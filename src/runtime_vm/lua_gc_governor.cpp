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
    // Stated once so a log says what this does without anyone reading the source.
    // Lua 5.1 ships with a pause of 200, meaning a new cycle starts once memory
    // has doubled. Everything below is more eager than that, which trades total
    // collector work for a smaller and steadier heap - a reasonable trade on a
    // 32-bit client, and one nobody has ever measured either way.
    Log("[GCGovernor] pause/stepmul: loading 160/150, combat 100/110, "
        "idle 110/400, otherwise 120/200 (stock Lua is 200/200)");
    return true;
}

void Shutdown() {
    g_initialized = false;
}

// Timing the collector.
//
// A tester profile put Lua's mark phase at 10.96%% of main-thread execution and
// its sweep at 7.90%% - the largest single cost inside the client. Another
// session with the same settings put the mark phase at 2.24%%. So the workload
// decides, not the configuration, and any claim about whether this governor
// helps or hurts is unfounded until the steps it asks for are timed.
//
// This times only the steps this module requests. Collection the VM starts on
// its own is not counted here; the sampling profiler covers that.
static double   g_gcStepMsTotal = 0.0;
static uint64_t g_gcStepCount   = 0;
static LARGE_INTEGER g_qpcFreq  = {};

static inline void StepTimed(void* L, int kb) {
    if (g_qpcFreq.QuadPart == 0) QueryPerformanceFrequency(&g_qpcFreq);

    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    g_lua_gc(L, 5, kb);          // LUA_GCSTEP
    QueryPerformanceCounter(&b);

    if (g_qpcFreq.QuadPart > 0) {
        g_gcStepMsTotal += (double)(b.QuadPart - a.QuadPart) * 1000.0
                         / (double)g_qpcFreq.QuadPart;
        g_gcStepCount++;
    }
}

void LogStats() {
    if (g_gcStepCount == 0) {
        Log("[GCGovernor] no collection steps requested this session");
        return;
    }
    Log("[GCGovernor] %llu steps requested, %.1f ms total, %.3f ms average",
        (unsigned long long)g_gcStepCount, g_gcStepMsTotal,
        g_gcStepMsTotal / (double)g_gcStepCount);
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
            StepTimed(L, 16);
        }
        return;
    }

    g_lua_gc(L, 1, 0); // Ensure restarted

    if (g_isIdle && frameMs < 8.0) {
        g_lua_gc(L, LUA_GCSETPAUSE, 110);
        g_lua_gc(L, LUA_GCSETSTEPMUL, 400);
        StepTimed(L, 1024);
    } else {
        g_lua_gc(L, LUA_GCSETPAUSE, 120);
        g_lua_gc(L, LUA_GCSETSTEPMUL, 200);
        
        int stepKB = (int)(diffKB * 1.5);
        if (stepKB < 62) stepKB = 62;
        if (stepKB > 512) stepKB = 512;
        
        StepTimed(L, stepKB);
    }
}

} // namespace LuaGCGovernor
