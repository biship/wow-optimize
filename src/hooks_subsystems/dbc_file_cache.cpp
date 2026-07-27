#include "dbc_file_cache.h"
#include "version.h"
#include <unordered_map>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace DbcFileCache {

struct DbcKey {
    void* dbc;
    uint32_t id;

    bool operator==(const DbcKey& o) const {
        return dbc == o.dbc && id == o.id;
    }
};

struct DbcKeyHash {
    size_t operator()(const DbcKey& k) const {
        return (size_t)k.dbc ^ (size_t)k.id;
    }
};

static std::unordered_map<DbcKey, void*, DbcKeyHash> g_dbcCache;
static WinMutex g_dbcMutex;
static uint64_t g_dbcHits = 0;
static uint64_t g_dbcMisses = 0;

bool Init() {
    Log("[DbcFileCache] Active - DBC File Query Cache initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_dbcMutex);
    g_dbcCache.clear();
    Log("[DbcFileCache] Stats: %lld hits, %lld misses", g_dbcHits, g_dbcMisses);
}


void* LookupRecord(void* dbc, uint32_t id) {
    if (!dbc) return nullptr;
    DbcKey key = {dbc, id};
    WinLockGuard lock(g_dbcMutex);
    auto it = g_dbcCache.find(key);
    if (it != g_dbcCache.end()) {
        g_dbcHits++;
        return it->second;
    }
    g_dbcMisses++;
    return nullptr;
}

} // namespace DbcFileCache
