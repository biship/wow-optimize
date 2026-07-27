#pragma once
#include <windows.h>
#include <cstdint>

namespace DbcFileCache {
    bool Init();
    void Shutdown();
    void* LookupRecord(void* dbc, uint32_t id);

}
