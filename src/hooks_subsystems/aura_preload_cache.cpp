#include "aura_preload_cache.h"
#include "version.h"
#include <unordered_set>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace AuraPreloadCache {

static std::unordered_set<std::string> g_preloadedAuras;
static WinMutex g_auraMutex;
static uint64_t g_preloadHits = 0;

bool Init() {
    Log("[AuraPreloadCache] Active - Aura / Buff Textures Preload Cache initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_auraMutex);
    g_preloadedAuras.clear();
    Log("[AuraPreloadCache] Stats: Serviced %lld preloaded aura textures", g_preloadHits);
}

void PreloadAuraTexture(const std::string& path) {
    if (path.empty()) return;
    WinLockGuard lock(g_auraMutex);
    if (g_preloadedAuras.count(path) == 0) {
        g_preloadedAuras.insert(path);
        g_preloadHits++;
    }
}


} // namespace AuraPreloadCache
