#include "lua_gc_governor.h"
#include "version.h"
#include "lua_optimize.h"
#include <atomic>
#include <emmintrin.h>

extern "C" void Log(const char* fmt, ...);

namespace LuaGCGovernor {

typedef int (__cdecl *lua_gc_fn)(void* L, int what, int data);
static lua_gc_fn g_lua_gc = (lua_gc_fn)0x0084ED50;

typedef int (__cdecl *lua_getfield_fn)(void* L, int idx, const char* k);
static lua_getfield_fn g_lua_getfield = (lua_getfield_fn)0x0084E590;

typedef int (__cdecl *lua_toboolean_fn)(void* L, int idx);
static lua_toboolean_fn g_lua_toboolean = (lua_toboolean_fn)0x0084E0B0;

typedef void (__cdecl *lua_settop_fn)(void* L, int idx);
static lua_settop_fn g_lua_settop = (lua_settop_fn)0x0084DBF0;

static inline bool ReadGlobalBool(void* L, const char* name) {
    if (!L) return false;
    g_lua_getfield(L, -10002, name);
    int val = g_lua_toboolean(L, -1);
    g_lua_settop(L, -2);
    return val != 0;
}

static inline double GetLuaMemoryKB(void* L) {
    if (!L) return 0.0;
    int count = g_lua_gc(L, 3, 0); // LUA_GCCOUNT
    int countb = g_lua_gc(L, 4, 0); // LUA_GCCOUNTB
    return (double)count + (double)countb / 1024.0;
}

bool g_inCombat = false;
bool g_isIdle = false;
bool g_isLoading = false;
static bool g_initialized = false;
static double g_lastMemoryKB = 0.0;

bool Init() {
    g_initialized = true;
    Log("[GCGovernor] Adaptive GC Governor Initialized");
    return true;
}

void Shutdown() {
    g_initialized = false;
}

void OnFrame(double frameMs) {
    if (!g_initialized) return;
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) return;

    void* L = *(void**)0x00D3F78C;
    if (!L) return;

    double memKB = GetLuaMemoryKB(L);
    double diffKB = memKB - g_lastMemoryKB;
    if (diffKB < 0.0) diffKB = 0.0;
    g_lastMemoryKB = memKB;

#define LUA_GCSETPAUSE   6
#define LUA_GCSETSTEPMUL 7

    if (g_isLoading) {
        // Relax GC during loading screen to accelerate addon instantiation without freezing VM
        g_lua_gc(L, LUA_GCSETPAUSE, 160);
        g_lua_gc(L, LUA_GCSETSTEPMUL, 150);
        return;
    }

    if (g_inCombat) {
        if (memKB < 256.0 * 1024.0) {
            g_lua_gc(L, 0, 0); // LUA_GCSTOP
        } else {
            g_lua_gc(L, 1, 0); // Ensure restarted
            g_lua_gc(L, LUA_GCSETPAUSE, 100);
            g_lua_gc(L, LUA_GCSETSTEPMUL, 110);
            g_lua_gc(L, 5, 16);
        }
        return;
    }

    g_lua_gc(L, 1, 0); // Ensure restarted

    if (g_isIdle && frameMs < 8.0) {
        g_lua_gc(L, LUA_GCSETPAUSE, 110);
        g_lua_gc(L, LUA_GCSETSTEPMUL, 400);
        g_lua_gc(L, 5, 1024);
    } else {
        g_lua_gc(L, LUA_GCSETPAUSE, 120);
        g_lua_gc(L, LUA_GCSETSTEPMUL, 200);
        
        int stepKB = (int)(diffKB * 1.5);
        if (stepKB < 62) stepKB = 62;
        if (stepKB > 512) stepKB = 512;
        
        g_lua_gc(L, 5, stepKB);
    }
}

} // namespace LuaGCGovernor
