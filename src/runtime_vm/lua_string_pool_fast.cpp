#include <windows.h>
#include <unordered_map>
#include <string>
#include "win_mutex.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace LuaStringPoolFast {

// Interned symbol pool
static std::unordered_map<std::string, void*> g_symbolPool;
static WinMutex g_poolMutex;
static uint64_t g_hits = 0;
static uint64_t g_misses = 0;

void* GetSymbol(const std::string& str) {
    WinLockGuard lock(g_poolMutex);
    auto it = g_symbolPool.find(str);
    if (it != g_symbolPool.end()) {
        g_hits++;
        return it->second;
    }
    g_misses++;
    return nullptr;
}

void InsertSymbol(const std::string& str, void* luaStringObj) {
    WinLockGuard lock(g_poolMutex);
    g_symbolPool[str] = luaStringObj;
}

bool Init() {
    Log("[LuaStringPoolFast] Active - Lua String Symbol Pool Cache Initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_poolMutex);
    g_symbolPool.clear();
    Log("[LuaStringPoolFast] Stats: %lld hits, %lld misses in string pool", g_hits, g_misses);
}


} // namespace LuaStringPoolFast
