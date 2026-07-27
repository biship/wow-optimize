#pragma once
#include <windows.h>

namespace MinimapRefreshGovernor {
    bool Init();
    void Shutdown();
    bool ShouldSkipRefresh();

}
