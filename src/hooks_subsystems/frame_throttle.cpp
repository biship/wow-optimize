// ============================================================================
// Module: frame_throttle.cpp
// Description: Lock-free high-performance UI script execution throttling.
// Safety & Threading: Thread-safe under single-threaded Lua VM execution constraints.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

struct ThrottleSlot {
    uint32_t nameHash;
    LARGE_INTEGER lastExecution;
};

static constexpr int THROTTLE_CACHE_SIZE = 512;
static ThrottleSlot g_throttleCache[THROTTLE_CACHE_SIZE] = {};
static LARGE_INTEGER g_qpcFreq = {};

// Stats
static long g_throttleSkipped = 0;
static long g_throttleExecuted = 0;
static long g_throttleBypassed = 0;

// Bypass list - critical scripts that should never be throttled
static const char* g_bypassScripts[] = {
    "OnUpdate",      // Generic OnUpdate (too broad, but safe)
    "OnEvent",       // Event handlers (critical)
    "OnLoad",        // Load handlers (one-time)
    "OnShow",        // Show handlers (infrequent)
    "OnHide",        // Hide handlers (infrequent)
    "OnDragStart",   // Mouse drag start (MoveAnything)
    "OnDragStop",    // Mouse drag stop (MoveAnything)
    "OnMouseDown",   // Mouse button down (MoveAnything)
    "OnMouseUp",     // Mouse button up (MoveAnything)
    "OnEnter",       // Mouse enter (tooltips)
    "OnLeave",       // Mouse leave (tooltips)
    "OnClick",       // Mouse click (buttons)
    nullptr
};

static bool ShouldBypassThrottle(const char* scriptName) {
    if (!scriptName) return false;
    
    // Bypass if script name contains critical keywords
    for (int i = 0; g_bypassScripts[i]; i++) {
        if (strstr(scriptName, g_bypassScripts[i])) {
            return true;
        }
    }
    
    // Bypass if script name is very short (likely critical)
    if (strlen(scriptName) < 10) {
        return true;
    }
    
    return false;
}

// Check if a script execution should be allowed or throttled
extern "C" bool FrameThrottle_Check(const char* namePtr) {
#if TEST_DISABLE_FRAME_THROTTLE
    return true; // Bypass all throttling if disabled
#else
    if (!namePtr || !*namePtr) return true;

    // Check bypass list
    if (ShouldBypassThrottle(namePtr)) {
        g_throttleBypassed++;
        return true;
    }

    // Get current time
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    // Compute fast hash
    uint32_t hash = 0x811C9DC5;
    const char* p = namePtr;
    while (*p) {
        hash ^= (uint8_t)*p++;
        hash *= 0x01000193;
    }

    int slot = hash % THROTTLE_CACHE_SIZE;
    ThrottleSlot& entry = g_throttleCache[slot];

    // QPC conversion for 16ms
    LONGLONG minInterval = (g_qpcFreq.QuadPart * 16) / 1000;

    if (entry.nameHash == hash) {
        LONGLONG elapsed = now.QuadPart - entry.lastExecution.QuadPart;
        if (elapsed < minInterval) {
            g_throttleSkipped++;
            return false; // Throttle / skip execution
        }
    } else {
        entry.nameHash = hash;
    }

    entry.lastExecution = now;
    g_throttleExecuted++;
    return true; // Execute
#endif
}

bool InstallFrameThrottling() {
#if TEST_DISABLE_FRAME_THROTTLE
    Log("[FrameThrottle] DISABLED (test toggle)");
    return false;
#else
    // Initialize QPC frequency
    QueryPerformanceFrequency(&g_qpcFreq);
    std::memset(g_throttleCache, 0, sizeof(g_throttleCache));
    Log("[FrameThrottle] ACTIVE (16ms lock-free inline throttle enabled)");
    return true;
#endif
}

void GetFrameThrottleStats(long* skipped, long* executed, long* bypassed) {
    if (skipped) *skipped = g_throttleSkipped;
    if (executed) *executed = g_throttleExecuted;
    if (bypassed) *bypassed = g_throttleBypassed;
}

void ShutdownFrameThrottling() {
    // No cleanup required for static array cache
}


