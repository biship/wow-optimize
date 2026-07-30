#pragma once
#include <windows.h>

namespace FontGlyphCache {
    bool Init();
    void Shutdown();
    void LogStats();
    void ClearCache();
}
