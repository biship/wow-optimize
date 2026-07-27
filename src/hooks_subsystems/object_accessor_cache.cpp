// ============================================================================
// Module: object_accessor_cache.cpp
// ============================================================================

#include "object_accessor_cache.h"
#include <windows.h>
#include <MinHook.h>

extern "C" void Log(const char* fmt, ...);

#define CACHE_SIZE 256

struct CacheEntry {
    void* obj_ptr;
    int field_offset;
    void* result;
};

static CacheEntry g_cache[CACHE_SIZE];
static CRITICAL_SECTION g_cache_lock;
static bool g_initialized = false;

typedef void* (__thiscall* object_accessor_t)(void* this_ptr, int field_offset);
static object_accessor_t orig_object_accessor = nullptr;

static inline unsigned int hash_entry(void* obj, int offset) {
    unsigned int h = (unsigned int)((uintptr_t)obj >> 2);
    h ^= (unsigned int)offset;
    return h % CACHE_SIZE;
}

static void* __cdecl hook_object_accessor(void* this_ptr, int field_offset) {
    if (!g_initialized) {
        InitializeCriticalSection(&g_cache_lock);
        memset(g_cache, 0, sizeof(g_cache));
        g_initialized = true;
    }

    // Check cache
    unsigned int idx = hash_entry(this_ptr, field_offset);
    EnterCriticalSection(&g_cache_lock);

    if (g_cache[idx].obj_ptr == this_ptr && g_cache[idx].field_offset == field_offset) {
        void* cached = g_cache[idx].result;
        LeaveCriticalSection(&g_cache_lock);
        return cached;
    }

    LeaveCriticalSection(&g_cache_lock);

    // Cache miss - call original
    void* result = orig_object_accessor(this_ptr, field_offset);

    // Store in cache
    EnterCriticalSection(&g_cache_lock);
    g_cache[idx].obj_ptr = this_ptr;
    g_cache[idx].field_offset = field_offset;
    g_cache[idx].result = result;
    LeaveCriticalSection(&g_cache_lock);

    return result;
}

bool InstallObjectAccessorCache() {
    void* target = (void*)0x007CECD0;
    
    if (MH_CreateHook(target, (void*)hook_object_accessor, (void**)&orig_object_accessor) != MH_OK) {
        Log("[ObjectAccessor] MH_CreateHook failed");
        return false;
    }
    
    if (MH_EnableHook(target) != MH_OK) {
        Log("[ObjectAccessor] MH_EnableHook failed");
        return false;
    }
    
    Log("[ObjectAccessor] Installed field cache (1886 callers, %d entries)", CACHE_SIZE);
    return true;
}

void UninstallObjectAccessorCache() {
    MH_DisableHook((void*)0x007CECD0);
    if (g_initialized) {
        DeleteCriticalSection(&g_cache_lock);
        g_initialized = false;
    }
}
