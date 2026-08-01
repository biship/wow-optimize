#pragma once
#include <windows.h>

namespace TextureUnloadDelay {
    bool Init();
    void Shutdown();
    void OnFrame();
    void Flush();
    void LogStats();
    void Discard();
}
