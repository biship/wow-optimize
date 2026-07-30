// ============================================================================
// Module: lua_bytecode_cache.cpp
// Description: Accelerates Lua runtime calls in `lua_bytecode_cache.cpp`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include "lua_bytecode_cache.h"
#include "version.h"
#include "MinHook.h"
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

extern "C" void Log(const char* fmt, ...);

namespace LuaBytecodeCache {

struct lua_State;
typedef int  (*lua_Writer)(lua_State*, const void*, size_t, void*);
typedef int  (__cdecl* luaL_loadbuffer_fn)(lua_State*, const char*, size_t, const char*);
typedef int  (__cdecl* lua_dump_fn)(lua_State*, lua_Writer, void*);
typedef void (__cdecl* lua_settop_fn)(lua_State*, int);
typedef int  (__cdecl* lua_gettop_fn)(lua_State*);

static constexpr uintptr_t ADDR_luaL_loadbuffer = 0x0084F860;
static constexpr uintptr_t ADDR_lua_dump        = 0x0084ED00;
static constexpr uintptr_t ADDR_lua_settop      = 0x0084DBF0;
static constexpr uintptr_t ADDR_lua_gettop      = 0x0084DBD0;

static const unsigned char LUA_SIGNATURE = 0x1B;

static luaL_loadbuffer_fn orig_luaL_loadbuffer = nullptr;
static lua_dump_fn        p_lua_dump           = nullptr;
static lua_settop_fn      p_lua_settop         = nullptr;
static lua_gettop_fn      p_lua_gettop         = nullptr;

static volatile LONG g_active = 0;
static __declspec(thread) volatile LONG g_inHook = 0;

struct Entry {
    std::vector<unsigned char> bytecode;
    DWORD lastUsed;
};
static std::unordered_map<uint64_t, Entry> g_cache;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;
static const size_t MAX_ENTRIES = 4096;
static const size_t MAX_BYTES   = 16 * 1024 * 1024;
static volatile LONG64 g_bytesCached = 0;

static volatile LONG64 g_hits = 0, g_misses = 0, g_bypasses = 0;

static uint64_t Fnv1a(const void* d, size_t n, uint64_t seed = 0xcbf29ce484222325ULL) {
    auto* p = (const unsigned char*)d;
    uint64_t h = seed;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}

static int DumpWriter(lua_State*, const void* p, size_t sz, void* ud) {
    auto* dst = (std::vector<unsigned char>*)ud;
    auto* b = (const unsigned char*)p;
    dst->insert(dst->end(), b, b + sz);
    return 0;
}

static void EvictOldest() {
    if (g_cache.size() <= MAX_ENTRIES && (uint64_t)g_bytesCached <= MAX_BYTES) return;
    auto old = g_cache.begin();
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it)
        if (it->second.lastUsed < old->second.lastUsed) old = it;
    InterlockedExchangeAdd64(&g_bytesCached, -(LONG64)old->second.bytecode.size());
    g_cache.erase(old);
}

static int HandleLoadBuffer(lua_State* L, const char* buf, size_t sz, const char* name) {
    int base = p_lua_gettop ? p_lua_gettop(L) : 0;
    int rc = 0;

    do {
        if (!buf || sz == 0) { 
            rc = orig_luaL_loadbuffer(L, buf, sz, name); 
            break; 
        }

        // Pointer validation guards
        if ((uintptr_t)buf < 0x10000 || (uintptr_t)buf > 0xFFE00000) {
            rc = orig_luaL_loadbuffer(L, buf, sz, name);
            break;
        }
        if (name && ((uintptr_t)name < 0x10000 || (uintptr_t)name > 0xFFE00000)) {
            rc = orig_luaL_loadbuffer(L, buf, sz, name);
            break;
        }

        // Already compiled bytecode - pass through directly
        if ((unsigned char)buf[0] == LUA_SIGNATURE) {
            rc = orig_luaL_loadbuffer(L, buf, sz, name);
            break;
        }

        uint64_t h = Fnv1a(buf, sz);
        if (name) h = Fnv1a(name, strlen(name), h);

        // Check cache
        std::vector<unsigned char> hitCopy;
        {
            AcquireSRWLockShared(&g_cacheLock);
            auto it = g_cache.find(h);
            if (it != g_cache.end()) {
                hitCopy = it->second.bytecode;
                InterlockedExchange((volatile LONG*)&it->second.lastUsed, (LONG)GetTickCount());
            }
            ReleaseSRWLockShared(&g_cacheLock);
        }

        if (!hitCopy.empty()) {
            rc = orig_luaL_loadbuffer(L, (const char*)hitCopy.data(), hitCopy.size(), name);
            if (rc == 0) {
                InterlockedIncrement64(&g_hits);
                break;
            }
            
            // Clear the Lua stack back to base on cached load failure to prevent corruption
            if (p_lua_settop) {
                p_lua_settop(L, base);
            }

            // Bytecode incompatible - evict
            AcquireSRWLockExclusive(&g_cacheLock);
            auto it = g_cache.find(h);
            if (it != g_cache.end()) {
                InterlockedExchangeAdd64(&g_bytesCached, -(LONG64)it->second.bytecode.size());
                g_cache.erase(it);
            }
            ReleaseSRWLockExclusive(&g_cacheLock);

            // Fall through to compile from source!
        }

        // Compile source
        rc = orig_luaL_loadbuffer(L, buf, sz, name);
        if (rc != 0) { 
            InterlockedIncrement64(&g_bypasses); 
            break; 
        }

        InterlockedIncrement64(&g_misses);

        if (!p_lua_dump) break;

        // Dump bytecode for caching
        std::vector<unsigned char> dumped;
        dumped.reserve(sz);
        int dr = p_lua_dump(L, DumpWriter, &dumped);
        if (dr == 0 && !dumped.empty()) {
            AcquireSRWLockExclusive(&g_cacheLock);
            EvictOldest();
            auto& e = g_cache[h];
            InterlockedExchangeAdd64(&g_bytesCached,
                (LONG64)dumped.size() - (LONG64)e.bytecode.size());
            e.bytecode = std::move(dumped);
            e.lastUsed = GetTickCount();
            ReleaseSRWLockExclusive(&g_cacheLock);
        }
    } while (false);

    return rc;
}

static int __cdecl Hook_luaL_loadbuffer(lua_State* L, const char* buf, size_t sz, const char* name) {
    if (!g_active || g_inHook) {
        return orig_luaL_loadbuffer(L, buf, sz, name);
    }

    g_inHook = 1;
    int rc = 0;
    __try {
        rc = HandleLoadBuffer(L, buf, sz, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rc = orig_luaL_loadbuffer(L, buf, sz, name);
    }
    g_inHook = 0;
    return rc;
}

bool Init() {
    p_lua_dump   = (lua_dump_fn)ADDR_lua_dump;
    p_lua_settop = (lua_settop_fn)ADDR_lua_settop;
    p_lua_gettop = (lua_gettop_fn)ADDR_lua_gettop;

    void* target = (void*)ADDR_luaL_loadbuffer;
    if (MH_CreateHook(target, (void*)Hook_luaL_loadbuffer, (void**)&orig_luaL_loadbuffer) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK)
        return false;

    InterlockedExchange(&g_active, 1);
    Log("[LuaBytecode] Active: %d-slot cache on luaL_loadbuffer (0x%X)", MAX_ENTRIES, ADDR_luaL_loadbuffer);
    return true;
}

// Printed from the periodic report. Shutdown does not run - the DLL exits via
// TerminateProcess - so anything reported only from there is never seen.
void LogStats() {
    Log("[LuaBytecode] Shutdown: hits=%lld misses=%lld", g_hits, g_misses);
}

void Shutdown() {
    InterlockedExchange(&g_active, 0);
    if (orig_luaL_loadbuffer) MH_DisableHook((void*)ADDR_luaL_loadbuffer);
    AcquireSRWLockExclusive(&g_cacheLock);
    g_cache.clear();
    g_bytesCached = 0;
    ReleaseSRWLockExclusive(&g_cacheLock);
    LogStats();
}

void OnLuaStateSwap() {
    if (!g_active) return;
    AcquireSRWLockExclusive(&g_cacheLock);
    g_cache.clear();
    g_bytesCached = 0;
    ReleaseSRWLockExclusive(&g_cacheLock);
    Log("[LuaBytecode] Cleared on lua_State swap");
}

void GetStats(Stats* out) {
    if (!out) return;
    out->hits   = (uint64_t)g_hits;
    out->misses = (uint64_t)g_misses;
    out->bypasses = (uint64_t)g_bypasses;
    out->bytesCached = (uint64_t)g_bytesCached;
    AcquireSRWLockShared(&g_cacheLock);
    out->entries = (uint32_t)g_cache.size();
    ReleaseSRWLockShared(&g_cacheLock);
}

} // namespace LuaBytecodeCache
