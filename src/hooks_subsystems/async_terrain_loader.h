#pragma once
#ifndef ASYNC_TERRAIN_LOADER_H
#define ASYNC_TERRAIN_LOADER_H

namespace AsyncTerrainLoader {
    bool Init();
    void Shutdown();
    bool IsGridLoading(void* grid);

}

#endif // ASYNC_TERRAIN_LOADER_H
