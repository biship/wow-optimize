#pragma once
#include <windows.h>
#include <string>

namespace AuraPreloadCache {
    bool Init();
    void Shutdown();
    void PreloadAuraTexture(const std::string& path);

}
