#include "minimap_refresh_governor.h"

namespace MinimapRefreshGovernor {
    static bool g_enabled = true;
    static DWORD g_lastRefreshTime = 0;
    static constexpr DWORD REFRESH_INTERVAL_MS = 100; // Cap to 10 FPS updates

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldSkipRefresh() {
        if (!g_enabled) return false;
        DWORD now = GetTickCount();
        if (now - g_lastRefreshTime < REFRESH_INTERVAL_MS) {
            return true; // Skip this refresh tick
        }
        g_lastRefreshTime = now;
        return false;
    }

}
