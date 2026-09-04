// ============================================================================
// Module: lua_rawgeti_inline.cpp
// Description: Accelerates Lua runtime calls in `lua_rawgeti_inline.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <emmintrin.h>
#include "MinHook.h"
#include "lua_rawgeti_inline.h"
#include "lua_optimize.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// ----------------------------------------------------------------
// Statistics (diagnostic only; plain increments)
// ----------------------------------------------------------------
// Whether the hook actually went in, so the report can tell a guard
// that never fired from one that was never installed.
static bool g_statsInstalled = false;

static volatile LONG64 g_total_calls = 0;
static volatile LONG64 g_array_hits = 0;
static volatile LONG64 g_nil_returns = 0;

void ClearRawGetIInlineCache() {
    // Cache removed to prevent GC/reload invalidation hazards.
}

// ----------------------------------------------------------------
// Original function pointer
// ----------------------------------------------------------------
typedef int (__cdecl *lua_rawgeti_fn)(int L, int idx, int n);
static lua_rawgeti_fn g_orig_rawgeti = nullptr;

// ----------------------------------------------------------------
// Optimized replacement — SAFE (no pointer caching)
// ----------------------------------------------------------------
#include "../allocators/loading_defrag.h"

static int __cdecl Optimized_RawGetI(int L, int idx, int n)
{
    ++g_total_calls;
    bool processed = false;
    int res_val = 0;

    // Bail out during lua_State swap or active loading
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping() || LoadingDefrag::IsLoadingActive()) {
        return g_orig_rawgeti(L, idx, n);
    }

    // Validate L pointer
    if ((uintptr_t)L < 0x10000 || (uintptr_t)L > 0xFFE00000) {
        return g_orig_rawgeti(L, idx, n);
    }

    __try {
        int* L_base = *(int**)(L + 0x10);  // L->base
        int* L_top  = *(int**)(L + 0x0C);  // L->top

        // Validate stack pointers
        if ((uintptr_t)L_base < 0x10000 || (uintptr_t)L_base > 0xFFE00000 ||
            (uintptr_t)L_top < 0x10000 || (uintptr_t)L_top > 0xFFE00000) {
            return g_orig_rawgeti(L, idx, n);
        }

        // Fast inline stack lookup: bypasses CallIndex2Adr function overhead
        int* tableSlot = nullptr;
        if (idx > 0) {
            int* targetSlot = L_base + (idx - 1) * 4; // 4 DWORDs = 16 bytes per TValue
            if (targetSlot < L_top) {
                tableSlot = targetSlot;
            }
        } else if (idx < 0 && idx > -10000) {
            int* targetSlot = L_top + idx * 4;
            if (targetSlot >= L_base) {
                tableSlot = targetSlot;
            }
        }

        // Pseudo-indices and RegistryTable defer to the engine
        if (!tableSlot) {
            return g_orig_rawgeti(L, idx, n);
        }

        int table = tableSlot[0];
        // Validate: must be a table (tt == 5)
        if (tableSlot[2] != 5 || table < 0x10000 || table > 0xFFE00000) {
            return g_orig_rawgeti(L, idx, n);
        }

        // ============================================================
        // FAST PATH: Direct array access
        // If (n-1) < sizearray, the value is in the array part.
        // This is O(1) with no cache needed.
        // ============================================================
        int sizearray = *(int*)(table + 32);
        if ((unsigned int)(n - 1) < (unsigned int)sizearray) {
            int* array = *(int**)(table + 16);
            if (!array || (uintptr_t)array < 0x10000 || (uintptr_t)array > 0xFFE00000) {
                return g_orig_rawgeti(L, idx, n);
            }

            int* src = array + (n - 1) * 4;  // 4 DWORDs = 16 bytes per TValue

            // Push TValue onto Lua stack (L->top)
            int* top = *(int**)(L + 0x0C);
            if (!top || (uintptr_t)top < 0x10000 || (uintptr_t)top > 0xFFE00000) {
                return g_orig_rawgeti(L, idx, n);
            }

            top[0] = src[0];  // value lo
            top[1] = src[1];  // value hi
            top[2] = src[2];  // tt
            top[3] = src[3];  // taint
            *(int**)(L + 0x0C) = top + 4;
            processed = true;

            // Taint propagation — replicate sub_84E670 EXACTLY
            DWORD taint = src[3];
            ++g_array_hits;
            if (taint) {
                if (*(int*)0x00D413A0 && !*(int*)0x00D413A4)
                    *(uint32_t*)0x00D4139C = taint;
                res_val = (int)taint;
            } else {
                DWORD gt = *(uint32_t*)0x00D4139C;
                top[3] = gt;
                res_val = (int)gt;
            }
            return res_val;
        }

        // ============================================================
        // HASH PART: Integer key not in array, let original handle it
        // ============================================================
        return g_orig_rawgeti(L, idx, n);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (processed) return res_val;
        return g_orig_rawgeti(L, idx, n);
    }
}

// ----------------------------------------------------------------
// Install / Uninstall
// ----------------------------------------------------------------
bool InstallLuaRawGetIInline()
{
    void* target = (void*)0x0084E670;

    // Verify prologue: push ebp; mov ebp, esp
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[RawGetIInline] BAD PROLOGUE at 0x%08X (expected 55 8B)", (uintptr_t)target);
        return false;
    }

    if (MH_CreateHook(target, (void*)Optimized_RawGetI, (void**)&g_orig_rawgeti) != MH_OK) {
        Log("[RawGetIInline] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[RawGetIInline] MH_EnableHook FAILED");
        return false;
    }

    Log("[RawGetIInline] Hook ACTIVE (fast inline stack lookup + array fast path)");
    g_statsInstalled = true;
    return true;
}

// Printed from the periodic report. The counters used to be printed only
// from the uninstall path, which nothing calls: the DLL leaves through
// TerminateProcess, and the linker had dropped the function outright.
void LuaRawGetIInline_LogStats(void) {
    if (!g_statsInstalled) {
        Log("[RawGetIInline] not measured: the hook is not installed.");
        return;
    }
    const LONG64 total = g_total_calls, arr = g_array_hits;
    if (total == 0) {
        Log("[RawGetIInline] measured and zero: no indexed read reached it.");
        return;
    }
    Log("[RawGetIInline] %lld calls, %lld from the array part (%.1f%%), "
        "%lld fell back.",
        (long long)total, (long long)arr,
        100.0 * (double)arr / (double)total, (long long)(total - arr));
}

void UninstallLuaRawGetIInline()
{
    MH_DisableHook((void*)0x0084E670);
    MH_RemoveHook((void*)0x0084E670);

    LONG64 total = g_total_calls;
    LONG64 arr   = g_array_hits;

    if (total > 0) {
        double arrPct   = 100.0 * arr / total;
        Log("[RawGetIInline] Stats: %lld calls | %lld array (%.1f%%) | %lld fallback",
            total, arr, arrPct, total - arr);
    }
}