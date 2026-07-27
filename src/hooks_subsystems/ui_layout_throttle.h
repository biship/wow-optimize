#pragma once
#include <windows.h>

namespace UILayoutThrottle {
    bool Init();
    void Shutdown();
    bool ShouldThrottle(void* frame);
    void ResetFrameCounter();

}
