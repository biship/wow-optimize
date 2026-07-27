#pragma once
#include <windows.h>

namespace VertexBufferPrealloc {
    bool Init();
    void Shutdown();
    void* AllocateBuffer(size_t size);
    void FreeBuffer(void* ptr);

}
