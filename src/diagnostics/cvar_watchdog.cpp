// ============================================================================
// Module: cvar_watchdog.cpp
// Description: Checks a list of globals the client is known to crash on when they
//              are null, and says so. Runs once, after the client is up.
// Safety & Threading: Main thread only.
//
// It used to run at injection, which is before the client has initialized any of
// these. Every global was legitimately zero at that point, so the one scan it ever
// performed reported nine "CORRUPT" findings about a game that had not started -
// including "lua_State (global Lua state) is NULL", which is the plainest possible
// statement that the scan was too early rather than that anything was wrong. Worse,
// the repair path writes safeValue over what it judges corrupt, so any entry given
// a non-zero safeValue would have planted a value into a global the client was
// about to initialize itself. No entry has one today, which is the only reason that
// never happened.
//
// The readiness signal is the global lua_State: once the client has one, it is far
// enough along that a null in this list means something.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

struct CvarWatchEntry {
    uintptr_t  addr;       // address of the CVar pointer global
    const char* name;      // CVar name for logging
    uint32_t   safeValue;  // safe default value (0 = just warn)
    bool       isPointer;  // true if the global is a pointer to a CVar struct
};

static CvarWatchEntry g_watch[] = {
    // ---- Render init globals (crash at 0x87307D) ----
    { 0x00D43024, "dword_D43024 (render init)",     0, false },
    { 0x00C5DF88, "dword_C5DF88 (render init)",     0, false },

    // ---- Sound CVars (crash at 0x4C5E7B, 0x4D1D53) ----
    { 0x00B4A3A4, "Sound_EnableSoundWhenGameIsInBG",      0, true  },
    { 0x00B4AEFC, "Sound_EnableReverb pointer",            0, true  },
    { 0x009F22CC, "Sound_EnableSoundWhenGameIsInBG (2)",   0, true  },
    { 0x009F23B0, "Sound_OutputDriverIndex",               0, true  },
    { 0x009F2488, "Sound_EnableAmbience",                  0, true  },
    { 0x009F24A0, "Sound_EnableMusic",                     0, true  },
    { 0x009F24C8, "Sound_EnableSFX",                       0, true  },
    { 0x009F24DC, "Sound_EnableAllSound",                  0, true  },
    { 0x009F2544, "Sound_ChaosMode",                       0, true  },
    { 0x009F2554, "Sound_EnableDSPEffects",                0, true  },
    { 0x009F256C, "Sound_MasterVolume",                    0, true  },
    { 0x009F2580, "Sound_OutputDriverName",                0, true  },
    { 0x009F2598, "Sound_NumChannels",                     0, true  },
    { 0x009F25AC, "Sound_EnableSoftwareHRTF",              0, true  },
    { 0x009F25C8, "Sound_EnableReverb",                    0, true  },
    { 0x009F25F0, "Sound_ZoneMusicNoDelay",                0, true  },
    { 0x009F2820, "Sound_ListenerAtCharacter",             0, true  },

    // ---- Visual alert (crash at 0x5E90D0) ----
    { 0x00C24238, "dword_C24238 (visual alert)",     0, false },

    // ---- Taint system (secure execution) ----
    { 0x00D4139C, "Taint cell (secure execution)",    0, false },
    { 0x00D413A0, "Taint check flag",                 0, false },

    // ---- Lua state ----
    { 0x00D3F78C, "lua_State* (global Lua state)",    0, true  },
    { 0x00D3F790, "lua_State stack pointer (L+4)",    0, true  },

    // ---- CRT heap ----
    { 0x00B31684, "_crtheap (CRT heap handle)",       0, false },
};

// The global lua_State, which is also entry "lua_State* (global Lua state)" below.
static const uintptr_t GLOBAL_LUA_STATE = 0x00D3F78C;

static bool g_scanned = false;

// Cheap enough to call every frame: one load and a compare until the client is up,
// then one scan, then a single predictable branch forever after.
void CvarWatchdog_Check()
{
    if (g_scanned) return;

    // Before the client has a Lua state, every global in the list is zero because
    // nothing has set it yet. There is nothing to judge and no verdict to give.
    if (*(uintptr_t*)GLOBAL_LUA_STATE < 0x10000) return;

    g_scanned = true;

    Log("[CvarWatchdog] Scanning %d critical CVars (client is up)...",
        (int)(sizeof(g_watch)/sizeof(g_watch[0])));

    int fixed = 0;
    bool anyCorrupt = false;
    for (size_t i = 0; i < sizeof(g_watch)/sizeof(g_watch[0]); i++) {
        CvarWatchEntry& e = g_watch[i];
        uintptr_t val = *(uintptr_t*)e.addr;

        if (e.isPointer) {
            if (val < 0x10000) {
                anyCorrupt = true;
                Log("[CvarWatchdog] CORRUPT: %s is NULL (0x%08X) — "
                    "dependent code may crash", e.name, e.addr);
                if (e.safeValue != 0) {
                    *(uintptr_t*)e.addr = e.safeValue;
                    fixed++;
                }
            }
        } else {
            if (val == 0 || val == 0xFFFFFFFF || val == 0xCDCDCDCD) {
                anyCorrupt = true;
                Log("[CvarWatchdog] CORRUPT: %s = 0x%08X at 0x%08X",
                    e.name, (unsigned)val, (unsigned)e.addr);
                if (e.safeValue != 0) {
                    *(uint32_t*)e.addr = e.safeValue;
                    fixed++;
                }
            }
        }
    }

    if (fixed > 0) {
        Log("[CvarWatchdog] Fixed %d corrupted CVars", fixed);
    } else if (anyCorrupt) {
        Log("[CvarWatchdog] Scan completed: corrupted CVars found (reported above).");
    } else {
        Log("[CvarWatchdog] All CVars OK");
    }
}

// Installs nothing. The scan itself is driven from the frame boundary, which is
// the only place that can tell when the client has finished starting.
bool InitCvarWatchdog()
{
    Log("[CvarWatchdog] Armed; will scan once the client has a Lua state");
    return true;
}
