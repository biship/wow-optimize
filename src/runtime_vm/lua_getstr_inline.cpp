// ============================================================================
// Module: lua_getstr_inline.cpp
// Description: Accelerates Lua runtime calls in `lua_getstr_inline.cpp`.
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
#include "lua_getstr_inline.h"
#include "hot_patch.h"
#include "lua_optimize.h"
#include "crash_dumper.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// ----------------------------------------------------------------
// Statistics (diagnostic only; plain increments. The Lua VM is
// single-threaded, so we use plain increments for minimum overhead.)
// ----------------------------------------------------------------
// Whether the hook actually went in, so the report can tell a guard
// that never fired from one that was never installed.
static bool g_statsInstalled = false;

static volatile LONG64 g_total_calls = 0;
static volatile LONG64 g_first_node_hits = 0;
static volatile LONG64 g_chain_walks = 0;
static volatile LONG64 g_chain_depth_total = 0;
static volatile LONG64 g_nil_returns = 0;

void InvalidateLuaGetStrInlineCache() {
    // Cache removed to prevent WeakAuras nil-field or GC invalidation errors.
}

// ----------------------------------------------------------------
// Nil object sentinel (returned when key not found)
// Located at 0xA46F78 in WoW 3.3.5a (resolved dynamically)
// ----------------------------------------------------------------
static void* g_nil_object = (void*)0x00A46F78;

// ----------------------------------------------------------------
// Original function pointer
// ----------------------------------------------------------------
typedef void* (__cdecl *luaH_getstr_fn)(int table, int tstring);
static luaH_getstr_fn g_orig_getstr = nullptr;

// ----------------------------------------------------------------
// Optimized replacement — SAFE (no pointer caching)
// ----------------------------------------------------------------
static void* __cdecl Optimized_GetStr(int table, int tstring)
{
    ++g_total_calls;

    // Bail out during lua_State swap — table and tstring pointers become
    // garbage when WoW destroys the old Lua VM during UI reload/logout.
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        return g_orig_getstr(table, tstring);
    }

    // Validate inputs — reject obviously invalid pointers
    if ((uintptr_t)table < 0x10000 || (uintptr_t)table > 0xFFE00000 ||
        (uintptr_t)tstring < 0x10000 || (uintptr_t)tstring > 0xFFE00000) {
        return g_orig_getstr(table, tstring);
    }

    // The entire node access is wrapped in SEH.
    __try {
        // Read tstring hash: tstring[12] = precomputed string hash
        uint32_t ts_hash = *(uint32_t*)(tstring + 12);

        // Read table metadata
        uint8_t  lsize      = *(uint8_t*)(table + 11);   // log2 of hash size
        uint32_t* node_array = *(uint32_t**)(table + 20); // hash bucket array

        if (!node_array || lsize == 0 || lsize > 24) {
            return g_orig_getstr(table, tstring);
        }

        // Compute bucket index: ts_hash & ((1 << lsize) - 1)
        uint32_t bucket_mask = (1u << lsize) - 1;
        uint32_t bucket_idx  = ts_hash & bucket_mask;

        // Get first node in chain (each node is 40 bytes = 10 DWORDs)
        uint32_t* node = (uint32_t*)((uint8_t*)node_array + 40 * bucket_idx);

        // FAST PATH: direct first-node check (~80% of lookups).
        if (node[6] == 4 && node[4] == (uint32_t)tstring) {
            ++g_first_node_hits;
            return node;
        }

        // SLOW PATH: walk the chain exactly like the engine — follow node[8]
        // until a match or NULL. NO early bounds-check break or state cache.
        ++g_chain_walks;
        int depth = 1;
        void* next = (void*)node[8];
        while (next != nullptr) {
            uint32_t* n = (uint32_t*)next;
            if (n[6] == 4 && n[4] == (uint32_t)tstring) {
                g_chain_depth_total += depth;
                return n;
            }
            next = (void*)n[8];
            depth++;
        }

        // End of chain, no match — return unk_A46F78 (nil object)
        ++g_nil_returns;
        return g_nil_object;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Corrupt chain/table — fall back to the engine rather than fault.
        return g_orig_getstr(table, tstring);
    }
}

// ----------------------------------------------------------------
// Install / Uninstall
// ----------------------------------------------------------------
bool InstallLuaGetStrInline()
{
    // Verified correct against the stock luaH_getstr decompile (0x85C430)
    void* target = (void*)0x0085C430;

    // Verify prologue: push ebp; mov ebp, esp
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[GetStrInline] BAD PROLOGUE at 0x%08X (expected 55 8B)", (uintptr_t)target);
        return false;
    }

    // Dynamically resolve nil object pointer from "mov eax, offset unk_A46F78" instruction
    // assembly at target + 0x38: B8 78 6F A4 00
    if (p[0x38] == 0xB8) {
        g_nil_object = *(void**)(p + 0x39);
        Log("[GetStrInline] Dynamically resolved nil object sentinel at %p", g_nil_object);
    } else {
        Log("[GetStrInline] WARNING: Failed to resolve nil object dynamically, using fallback %p", g_nil_object);
    }

    if (MH_CreateHook(target, (void*)Optimized_GetStr, (void**)&g_orig_getstr) != MH_OK) {
        Log("[GetStrInline] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[GetStrInline] MH_EnableHook FAILED");
        return false;
    }

    Log("[GetStrInline] Hook ACTIVE (safe first-node fast path + chain walk)");
    g_statsInstalled = true;
    return true;
}

// Printed from the periodic report. The counters used to be printed only
// from the uninstall path, which nothing calls: the DLL leaves through
// TerminateProcess, and the linker had dropped the function outright.
void LuaGetStrInline_LogStats(void) {
    if (!g_statsInstalled) {
        Log("[GetStrInline] not measured: the hook is not installed.");
        return;
    }
    const LONG64 total = g_total_calls, first = g_first_node_hits;
    const LONG64 walks = g_chain_walks, nils = g_nil_returns;
    const LONG64 depth = g_chain_depth_total;
    if (total == 0) {
        Log("[GetStrInline] measured and zero: no string lookup reached it.");
        return;
    }
    Log("[GetStrInline] %lld calls, %lld first-node (%.1f%%), %lld chain walks "
        "(avg depth %.1f), %lld nil.",
        (long long)total, (long long)first, 100.0 * (double)first / (double)total,
        (long long)walks, walks > 0 ? (double)depth / (double)walks : 0.0,
        (long long)nils);
}

void UninstallLuaGetStrInline()
{
    MH_DisableHook((void*)0x0085C430);
    MH_RemoveHook((void*)0x0085C430);

    LONG64 total = g_total_calls;
    LONG64 first = g_first_node_hits;
    LONG64 walks = g_chain_walks;
    LONG64 nils  = g_nil_returns;
    LONG64 depth = g_chain_depth_total;

    if (total > 0) {
        double firstPct = 100.0 * first / total;
        double avgDepth = walks > 0 ? (double)depth / walks : 0;
        Log("[GetStrInline] Stats: %lld calls | %lld first-node (%.1f%%) | "
            "%lld walks (avg depth %.1f) | %lld nil",
            total, first, firstPct, walks, avgDepth, nils);
    }
}