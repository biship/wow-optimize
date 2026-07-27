// ============================================================================
// Module: cache_governor.cpp
// ============================================================================

#include "cache_governor.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace CacheGovernor {

// Instance-type global, shared with combatlog_mt.cpp.
static constexpr uintptr_t kInstanceTypeAddr = 0x00B6AA38;

static volatile LONG  g_bumpAccum[CG_COUNT]   = {0};
static          LONG  g_bumpsLastWin[CG_COUNT] = {0};
static volatile LONG  g_bypassIssued[CG_COUNT] = {0};

static volatile LONG  g_inRaid       = 0;
static volatile LONG  g_instanceType = 0;
static volatile LONG  g_active       = 0;
static DWORD          g_lastWindowTick = 0;

static const DWORD WINDOW_MS          = 1000;
static const LONG  HOT_BUMP_THRESHOLD = 16;

static volatile LONG64 g_totalBumps    = 0;
static volatile LONG64 g_totalBypasses = 0;

static bool ReadIsRaid(int* outType) {
    __try {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void*)kInstanceTypeAddr, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & PAGE_NOACCESS) return false;
        int t = *(volatile int*)kInstanceTypeAddr;
        if (outType) *outType = t;
        return (t == 2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Init() {
    for (int i = 0; i < CG_COUNT; i++) {
        g_bumpAccum[i] = 0; g_bumpsLastWin[i] = 0; g_bypassIssued[i] = 0;
    }
    g_lastWindowTick = GetTickCount();
    InterlockedExchange(&g_active, 1);
    Log("[CacheGovernor] initialized (raid-flag @ 0x%08X, hot threshold=%d/sec)",
       (unsigned)kInstanceTypeAddr, HOT_BUMP_THRESHOLD);
    return true;
}

void Shutdown() {
    InterlockedExchange(&g_active, 0);
    Log("[CacheGovernor] shutdown total bumps=%lld bypasses=%lld",
       (long long)g_totalBumps, (long long)g_totalBypasses);
}

void OnFrame() {
    if (!g_active) return;
    DWORD now = GetTickCount();
    if ((LONG)(now - g_lastWindowTick) < (LONG)WINDOW_MS) return;

    for (int i = 0; i < CG_COUNT; i++)
        g_bumpsLastWin[i] = InterlockedExchange(&g_bumpAccum[i], 0);
    g_lastWindowTick = now;

    int it = 0;
    bool raid = ReadIsRaid(&it);
    InterlockedExchange(&g_instanceType, it);
    LONG prev = InterlockedExchange(&g_inRaid, raid ? 1 : 0);
    if (prev != (raid ? 1 : 0))
        Log("[CacheGovernor] raid mode = %s (instance type=%d)", raid ? "ON" : "off", it);
}

bool IsInRaid() { return g_active && g_inRaid != 0; }

void Bump(CacheId id) {
    if (!g_active || (uint32_t)id >= CG_COUNT) return;
    InterlockedIncrement(&g_bumpAccum[id]);
    InterlockedIncrement64(&g_totalBumps);
}

static bool IsHot(CacheId id) { return g_bumpsLastWin[id] >= HOT_BUMP_THRESHOLD; }

bool ShouldBypass(CacheId id) {
    if (!g_active || (uint32_t)id >= CG_COUNT) return false;
    bool bypass = false;
    if (g_inRaid && g_bumpsLastWin[id] > 0)        bypass = true;
    if (g_bumpsLastWin[id] >= HOT_BUMP_THRESHOLD)  bypass = true;
    if (bypass) {
        InterlockedIncrement(&g_bypassIssued[id]);
        InterlockedIncrement64(&g_totalBypasses);
    }
    return bypass;
}

float TtlMultiplier(CacheId id) {
    if (!g_active || (uint32_t)id >= CG_COUNT) return 1.0f;
    if (g_inRaid && IsHot(id)) return 0.0f;
    if (g_inRaid)              return 0.25f;
    if (IsHot(id))             return 0.5f;
    return 1.0f;
}

void GetStats(Stats* out) {
    if (!out) return;
    out->active        = g_active != 0;
    out->inRaid        = g_inRaid != 0;
    out->instanceType  = g_instanceType;
    for (int i = 0; i < CG_COUNT; i++) {
        out->bumpsLastWindow[i] = (uint32_t)g_bumpsLastWin[i];
        out->bypassesIssued[i]  = (uint32_t)g_bypassIssued[i];
    }
    out->totalBumps    = (uint64_t)g_totalBumps;
    out->totalBypasses = (uint64_t)g_totalBypasses;
}

} // namespace CacheGovernor
