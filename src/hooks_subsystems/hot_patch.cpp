// ============================================================================
// Module: hot_patch.cpp
// Description: lua_type fast path. Resolves a positive stack index inline and
//              returns the type tag without calling the engine's index2adr.
// Safety & Threading: Installed on the main thread. The fast path itself runs
//              on the Lua thread only.
// ============================================================================

#include "hot_patch.h"
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include "sampling_profiler.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// lua_type fast path (sub_84DEB0)
//
// The engine's lua_type is a thin wrapper: it calls index2adr
// (sub_84D9C0), compares the result against luaO_nilobject and returns
// either LUA_TNONE or the type tag. Transcribed from the binary:
//
//   sub_84DEB0:  ecx = L, eax = idx
//                call sub_84D9C0
//                cmp eax, offset unk_A46F78   ; luaO_nilobject
//                jnz short loc_84DECA
//                or  eax, 0FFFFFFFFh          ; LUA_TNONE
//                retn
//     loc_84DECA: mov eax, [eax+8]            ; type tag at +8
//                retn
//
//   sub_84D9C0 (idx > 0 branch):
//                mov edx, [ecx+10h]           ; L->base
//                shl eax, 4                   ; TValue stride is 16
//                lea eax, [edx+eax-10h]       ; base + (idx-1)
//                cmp eax, [ecx+0Ch]           ; L->top
//                jb  return eax               ; in range
//                mov eax, offset unk_A46F78   ; otherwise nil object
//
// So for a positive index the whole call collapses to two loads and a
// bounds check. Anything else - negative indices, pseudo-indices, an
// index at or past the top - falls through to the original, which keeps
// the LUA_TNONE and upvalue/registry/globals cases exactly as they are.
// ================================================================
static int g_featureToken = -1;
static unsigned int g_typeFastHits = 0;
static unsigned int g_typeFastMisses = 0;

typedef int (__cdecl *LuaType_fn)(int L, int idx);
static LuaType_fn orig_LuaType = nullptr;

static int __cdecl Hooked_LuaType(int L, int idx) {
    if (idx > 0 && L > 0x10000) {
        __try {
            int* base = *(int**)(L + 0x10);
            int* top  = *(int**)(L + 0x0C);
            int* slot = base + (idx - 1) * 4;   // TValue is 4 DWORDs
            if (slot < top && slot >= base) {
                int tt = slot[2];               // type tag at +8
                // Plain increment: lua_type runs on the single Lua thread and
                // this path fires tens of millions of times per session. A
                // locked atomic here costs more than the index2adr call the
                // fast path saves, which made the hook net-negative.
                ++g_typeFastHits;
                CrashDumper::FeatureHit(g_featureToken);
                return tt;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    ++g_typeFastMisses;
    return orig_LuaType(L, idx);
}

namespace HotPatch {
    bool InstallAll() {
        void* target = (void*)0x0084DEB0;
        if (WineSafe_CreateHook(target, (void*)Hooked_LuaType, (void**)&orig_LuaType) != MH_OK) {
            Log("[HotPatch] lua_type fast path: MH_CreateHook FAILED");
            return false;
        }
        if (MH_EnableHook(target) != MH_OK) {
            Log("[HotPatch] lua_type fast path: MH_EnableHook FAILED");
            return false;
        }
        g_featureToken = CrashDumper::FeatureTokenForCounting("HotPatch");
        SamplingProfiler::RegisterSelfSymbol("lua_type_fast", (const void*)&Hooked_LuaType);
        Log("[HotPatch] lua_type fast path: ACTIVE");
        return true;
    }

    void ShutdownAll() {
        DumpStats();
    }

    void DumpStats() {
        unsigned int total = g_typeFastHits + g_typeFastMisses;
        if (total == 0) {
            Log("[HotPatch] lua_type: no calls recorded");
            return;
        }
        Log("[HotPatch] lua_type: %u calls, %u inlined, %u forwarded (%.1f%% inlined)",
            total, g_typeFastHits, g_typeFastMisses,
            (double)g_typeFastHits * 100.0 / (double)total);
    }
}
