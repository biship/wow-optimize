#pragma once

// ============================================================================
// Module: cache_governor.h
// ============================================================================










// Central policy: caches consult ShouldBypass()/TtlMultiplier() to avoid
// caching values that will turn stale fast (raid mode + churn rate).

#include <windows.h>
#include <cstdint>

namespace CacheGovernor {

enum CacheId : uint32_t {
    CG_API_CACHE        = 0,
    CG_TOOLTIP_CACHE    = 1,
    CG_SPELL_CACHE      = 2,
    CG_UI_CACHE         = 3,
    CG_GETSPELLINFO     = 4,
    CG_GETPROCADDR      = 5,
    CG_ENVVAR           = 6,
    CG_LUA_FILE         = 7,
    CG_LUA_BYTECODE     = 8,
    CG_TEXTURE          = 9,
    CG_MODEL            = 10,
    CG_COUNT
};

bool Init();
void Shutdown();
void OnFrame();

bool  ShouldBypass(CacheId id);
float TtlMultiplier(CacheId id);
void  Bump(CacheId id);
bool  IsInRaid();

struct Stats {
    bool     active;
    bool     inRaid;
    int      instanceType;          // 0=none 1=party 2=raid 3=pvp 4=arena
    uint32_t bumpsLastWindow[CG_COUNT];
    uint32_t bypassesIssued[CG_COUNT];
    uint64_t totalBumps;
    uint64_t totalBypasses;
};
void GetStats(Stats* out);

} // namespace CacheGovernor
