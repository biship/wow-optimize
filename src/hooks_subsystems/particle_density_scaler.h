#pragma once
#include <windows.h>

namespace ParticleDensityScaler {
    bool Init();
    void Shutdown();
    void OnFrame(float elapsedMs);

}
