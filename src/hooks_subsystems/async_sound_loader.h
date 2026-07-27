#pragma once
#include <string>

namespace AsyncSoundLoader {
    bool Init();
    void Shutdown();
    void PreloadSound(const std::string& filePath);


}
