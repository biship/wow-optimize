// ============================================================================
// Module: async_terrain_loader.cpp
// Description: Asynchronous Terrain Chunk (ADT) Mesh & Collision Compiler
// Safety & Threading: Win32 CreateThread workers with safe SEH guards.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <unordered_set>
#include "MinHook.h"
#include "async_terrain_loader.h"
#include "terrain_height_cache.h"
#include "core/config.h"
#include "../allocators/loading_defrag.h"
#include "../core/version.h"
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace AsyncTerrainLoader {

typedef int (__cdecl *sub_7D9A20_fn)(void* grid);
static sub_7D9A20_fn orig_sub_7D9A20 = nullptr;

typedef int (__cdecl *sub_7C3700_fn)(void* grid);
static sub_7C3700_fn orig_sub_7C3700 = nullptr;

typedef int (__cdecl *sub_7C1660_fn)(float* pos, float* out_height, void** out_chunk);
static sub_7C1660_fn orig_sub_7C1660 = nullptr;

typedef void* (__thiscall *sub_7D6BF0_fn)(void* This, int a2, void* a3);
static sub_7D6BF0_fn orig_sub_7D6BF0 = nullptr;

static std::unordered_set<void*> g_loadingGrids;
static WinMutex g_loadingGridsMutex;
static std::atomic<int> g_asyncLoadCount{0};

bool IsGridLoading(void* grid) {
    if (!grid) return false;
    WinLockGuard lock(g_loadingGridsMutex);
    return g_loadingGrids.count(grid) > 0;
}

static void SafeLoadAdt(void* grid, sub_7D9A20_fn orig_fn) {
    __try {
        if (orig_fn) orig_fn(grid);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[AsyncTerrainLoader] Exception in background ADT load");
    }
}

struct AdtWorkerParam {
    void* grid;
    sub_7D9A20_fn orig_fn;
};

static DWORD WINAPI AdtWorkerProc(LPVOID lpParam) {
    AdtWorkerParam* p = (AdtWorkerParam*)lpParam;
    if (p) {
        SafeLoadAdt(p->grid, p->orig_fn);
        {
            WinLockGuard lock(g_loadingGridsMutex);
            g_loadingGrids.erase(p->grid);
        }
        g_asyncLoadCount.fetch_sub(1, std::memory_order_relaxed);
        delete p;
    }
    return 0;
}

int __cdecl Hooked_LoadAdt(void* grid) {
    if (!grid) return 0;

    // If loading screen is active, perform load synchronously
    if (LoadingDefrag::IsLoadingActive()) {
        return orig_sub_7D9A20 ? orig_sub_7D9A20(grid) : 0;
    }

    {
        WinLockGuard lock(g_loadingGridsMutex);
        if (g_loadingGrids.count(grid)) {
            return 0; // Already loading
        }
        g_loadingGrids.insert(grid);
    }

    g_asyncLoadCount.fetch_add(1, std::memory_order_relaxed);

    AdtWorkerParam* param = new AdtWorkerParam{ grid, orig_sub_7D9A20 };
    HANDLE hThread = CreateThread(NULL, 0, AdtWorkerProc, param, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        // Fallback synchronous load if thread creation failed
        SafeLoadAdt(grid, orig_sub_7D9A20);
        {
            WinLockGuard lock(g_loadingGridsMutex);
            g_loadingGrids.erase(grid);
        }
        g_asyncLoadCount.fetch_sub(1, std::memory_order_relaxed);
        delete param;
    }

    return 1;
}

int __cdecl Hooked_UnloadGrid(void* grid) {
    if (grid) {
        // Wait if the grid is currently loading in the background
        while (true) {
            {
                WinLockGuard lock(g_loadingGridsMutex);
                if (g_loadingGrids.count(grid) == 0) {
                    break;
                }
            }
            Sleep(1);
        }
    }
    return orig_sub_7C3700 ? orig_sub_7C3700(grid) : 0;
}

int __cdecl Hooked_GetGroundElevation(float* pos, float* out_height, void** out_chunk) {
    if (pos && out_height) {
        if (::TerrainHeightCache::GetCachedHeight(pos[0], pos[1], *out_height)) {
            // Only return cached height if out_chunk is null or caller does not demand a chunk pointer
            if (!out_chunk) {
                return 1;
            }
        }
    }
    int result = orig_sub_7C1660 ? orig_sub_7C1660(pos, out_height, out_chunk) : 0;
    if (result && pos && out_height) {
        ::TerrainHeightCache::AddToCache(pos[0], pos[1], *out_height);
    }
    return result;
}

void* __fastcall Hooked_CMapGrid_Update(void* This, void* unused, int a2, void* a3) {
    if (This && IsGridLoading(This)) {
        return a3; // Skip updating sub-chunks while ADT is still loading in background
    }
    return orig_sub_7D6BF0 ? orig_sub_7D6BF0(This, a2, a3) : a3;
}

bool Init() {
    if (!Config::g_settings.OptAsyncTerrainLoader) {
        Log("[AsyncTerrainLoader] DISABLED via configuration");
        return true;
    }
#if defined(TEST_DISABLE_ASYNC_TERRAIN) && TEST_DISABLE_ASYNC_TERRAIN == 1
    Log("[AsyncTerrainLoader] Disabled via TEST_DISABLE_ASYNC_TERRAIN");
    return true;
#endif
    Log("[AsyncTerrainLoader] Initializing Asynchronous Terrain Mesh Loader & Collision Decoupler");

    if (WineSafe_CreateHook((LPVOID)0x007D9A20, (LPVOID)Hooked_LoadAdt, (LPVOID*)&orig_sub_7D9A20) != MH_OK ||
        WineSafe_CreateHook((LPVOID)0x007C3700, (LPVOID)Hooked_UnloadGrid, (LPVOID*)&orig_sub_7C3700) != MH_OK ||
        WineSafe_CreateHook((LPVOID)0x007C1660, (LPVOID)Hooked_GetGroundElevation, (LPVOID*)&orig_sub_7C1660) != MH_OK ||
        WineSafe_CreateHook((LPVOID)0x007D6BF0, (LPVOID)Hooked_CMapGrid_Update, (LPVOID*)&orig_sub_7D6BF0) != MH_OK) {
        Log("[AsyncTerrainLoader] Failed to create hooks");
        return false;
    }

    if (WO_EnableHook((LPVOID)0x007D9A20) != MH_OK ||
        WO_EnableHook((LPVOID)0x007C3700) != MH_OK ||
        WO_EnableHook((LPVOID)0x007C1660) != MH_OK ||
        WO_EnableHook((LPVOID)0x007D6BF0) != MH_OK) {
        Log("[AsyncTerrainLoader] Failed to enable hooks");
        return false;
    }

    Log("[AsyncTerrainLoader] Active - Asynchronous ADT loading & collision decoupling active");
    return true;
}

void Shutdown() {
    if (!Config::g_settings.OptAsyncTerrainLoader) return;
#if defined(TEST_DISABLE_ASYNC_TERRAIN) && TEST_DISABLE_ASYNC_TERRAIN == 1
    return;
#endif
    MH_DisableHook((LPVOID)0x007D9A20);
    MH_DisableHook((LPVOID)0x007C3700);
    MH_DisableHook((LPVOID)0x007C1660);
    MH_DisableHook((LPVOID)0x007D6BF0);

    while (g_asyncLoadCount.load(std::memory_order_relaxed) > 0) {
        Sleep(1);
    }

    WinLockGuard lock(g_loadingGridsMutex);
    g_loadingGrids.clear();
}


} // namespace AsyncTerrainLoader
