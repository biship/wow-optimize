// ============================================================================
// Module: lua_rawget_inline.cpp
// Description: Accelerates Lua runtime calls in `lua_rawget_inline.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include "lua_rawget_inline.h"
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include "lua_optimize.h"

extern "C" void Log(const char* fmt, ...);

// Statistics
// Whether the hook actually went in, so the report can tell a guard
// that never fired from one that was never installed.
static bool g_statsInstalled = false;

static volatile long g_rawgetCalls = 0;
static volatile long g_rawgetFast  = 0;

#define TAINT_CELL ( *(uint32_t**)0x00D4139C )

typedef int (__cdecl* lua_rawget_fn)(uintptr_t L, int idx);
static lua_rawget_fn orig_rawget = nullptr;

static __forceinline bool IsValidPtr(uintptr_t p) {
    return p > 0x10000 && p < 0xFFE00000;
}

// Hooked lua_rawget (0x84E600): reads table at idx, lookup using key at L->top - 1,
// replaces the key at L->top - 1 with the retrieved value. Stack height doesn't change.
static int __cdecl Hooked_RawGet(uintptr_t L, int idx) {
    ++g_rawgetCalls;

    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        return orig_rawget(L, idx);
    }

    if (L < 0x10000 || L > 0xFFE00000) {
        return orig_rawget(L, idx);
    }

    __try {
        int* L_base = *(int**)(L + 0x10);  // L->base
        int* L_top  = *(int**)(L + 0x0C);  // L->top

        // Validate stack pointers
        if ((uintptr_t)L_base < 0x10000 || (uintptr_t)L_base > 0xFFE00000 ||
            (uintptr_t)L_top < 0x10000 || (uintptr_t)L_top > 0xFFE00000) {
            return orig_rawget(L, idx);
        }

        // Fast inline stack lookup
        int* tableSlot = nullptr;
        if (idx > 0) {
            int* targetSlot = L_base + (idx - 1) * 4;
            if (targetSlot < L_top) {
                tableSlot = targetSlot;
            }
        } else if (idx < 0 && idx > -10000) {
            int* targetSlot = L_top + idx * 4;
            if (targetSlot >= L_base) {
                tableSlot = targetSlot;
            }
        }

        if (tableSlot && IsValidPtr((uintptr_t)tableSlot) && tableSlot[2] == 5) { // LUA_TTABLE
            uintptr_t top = *(uintptr_t*)(L + 0x0C);
            uintptr_t key = top - 16;
            uintptr_t base = *(uintptr_t*)(L + 0x10);
            if (IsValidPtr(top) && IsValidPtr(base) && key >= base) {
                uintptr_t tab = *(uintptr_t*)(tableSlot + 0);
                if (IsValidPtr(tab)) {
                    // Call the engine's luaH_get (0x0085C470)
                    typedef uintptr_t (__cdecl *luaH_get_t)(uintptr_t t, uintptr_t key);
                    luaH_get_t luaH_get = (luaH_get_t)0x0085C470;
                    uintptr_t val = luaH_get(tab, key);
                    if (IsValidPtr(val)) {
                        // Replace key at stack top with value
                        *(uint64_t*)(key + 0) = *(uint64_t*)(val + 0);
                        *(int*)(key + 8) = *(int*)(val + 8);
                        
                        // Handle taint — engine checks val+12 (taint), not val+8 (tt)
                        uint32_t found_taint = *(uint32_t*)(val + 12);
                        if (found_taint == 0) {
                            uint32_t gt = *(uint32_t*)0x00D4139C;
                            *(uint32_t*)(key + 12) = gt;
                            ++g_rawgetFast;
                            return (int)gt;
                        } else {
                            if (*(uint32_t*)0x00D413A0 && !*(uint32_t*)0x00D413A4) {
                                *(uint32_t*)0x00D4139C = found_taint;
                            }
                            *(uint32_t*)(key + 12) = found_taint;
                            ++g_rawgetFast;
                            return (int)found_taint;
                        }
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return orig_rawget(L, idx);
}

bool InstallLuaRawGetInline() {
    void* target = (void*)0x0084E600;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[LuaRawGet] BAD PROLOGUE at 0x%08X (expected 55 8B)", (uintptr_t)target);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_RawGet, (void**)&orig_rawget) != MH_OK) {
        Log("[LuaRawGet] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[LuaRawGet] MH_EnableHook FAILED");
        return false;
    }
    Log("[LuaRawGet] ACTIVE: inline lua_rawget (0x84E600)");
    CrashDumper::RegisterFeature("LuaRawGet");
    CrashDumper::FeatureSetActive("LuaRawGet", true);
    g_statsInstalled = true;
    return true;
}

// Printed from the periodic report. The counters used to be printed only
// from the uninstall path, which nothing calls: the DLL leaves through
// TerminateProcess, and the linker had dropped the function outright.
void LuaRawGetInline_LogStats(void) {
    if (!g_statsInstalled) {
        Log("[LuaRawGet] not measured: the hook is not installed.");
        return;
    }
    const LONG64 total = g_rawgetCalls, fast = g_rawgetFast;
    if (total == 0) {
        Log("[LuaRawGet] measured and zero: no raw read reached it.");
        return;
    }
    Log("[LuaRawGet] %lld calls, %lld inline (%.1f%%).",
        (long long)total, (long long)fast,
        100.0 * (double)fast / (double)total);
}

void UninstallLuaRawGetInline() {
    MH_DisableHook((void*)0x0084E600);
    MH_RemoveHook((void*)0x0084E600);
    CrashDumper::FeatureSetActive("LuaRawGet", false);
    LONG64 total = g_rawgetCalls;
    LONG64 fast  = g_rawgetFast;
    if (total > 0) {
        Log("[LuaRawGet] Stats: %lld calls, %lld inline (%.1f%%)",
            (long long)total, (long long)fast,
            100.0 * (double)fast / (double)total);
    }
}
