#pragma once
#include <windows.h>

namespace LuaGCGovernor {
    extern bool g_inCombat;
    extern bool g_isLoading;
    extern bool g_isIdle;

    bool Init();
    void Shutdown();
    void OnFrame(double frameMs);

    // How much time the collection steps this module requests actually took.
    void LogStats();
}
