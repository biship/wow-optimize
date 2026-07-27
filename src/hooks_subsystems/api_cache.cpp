// ============================================================================
// Module: api_cache.cpp
// ============================================================================

#include "api_cache.h"
#include "item_data_prefetch.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// Forward declaration
typedef struct lua_State lua_State;
typedef double lua_Number;

// ================================================================
// TValue layout
// Matches RawTValue from lua_fastpath.cpp exactly.
//
// Offset 0:  Value union (void* gc / double n) - 8 bytes
// Offset 8:  tt (int) - type tag
// Offset 12: taint (uint32_t)
// Total: 16 bytes per TValue
// ================================================================

union RawValue {
    void*     gc;
    uintptr_t ptr;
    double    n;
};

struct RawTValue {
    RawValue  value;
    int       tt;
    uint32_t  taint;
};

// Stack base pointer is at L + 0x10
static inline RawTValue* GetStackBase(lua_State* L) {
    return *(RawTValue**)((uintptr_t)L + 0x10);
}

// TString layout:
// Offset 0-7:   CommonHeader (gc header)
// Offset 8:     len (int) - string length
// Offset 12:    hash (unsigned int)
// Offset 16:    str[0] - string data (flexible array member)
static inline const char* ReadTStringDirect(RawTValue* tv, size_t* out_len) {
    if (tv->tt != 4) return NULL;  // LUA_TSTRING

    void* ts_ptr = tv->value.gc;
    if ((uintptr_t)ts_ptr < 0x10000 || (uintptr_t)ts_ptr >= 0xFFFFF000) return NULL;

    // Read length directly from TString header (len is at offset 16 in WoW 3.3.5a)
    int len = *(int*)((char*)ts_ptr + 16);
    if (len < 0 || len > 1024) return NULL;

    char* str = (char*)ts_ptr + 20;
    if (out_len) *out_len = (size_t)len;
    return str;
}

static inline double ReadTNumberDirect(RawTValue* tv) {
    if (tv->tt != 3) return 0.0;  // LUA_TNUMBER
    double d;
    memcpy(&d, &tv->value.n, sizeof(double));
    return d;
}

// ================================================================

typedef int (__cdecl *ScriptFunc_fn)(lua_State* L);

typedef const char* (__cdecl *fn_lua_tolstring)(lua_State* L, int index, size_t* len);
typedef lua_Number (__cdecl *fn_lua_tonumber)(lua_State* L, int index);
typedef int        (__cdecl *fn_lua_gettop)(lua_State* L);
typedef int        (__cdecl *fn_lua_type)(lua_State* L, int index);
typedef void       (__cdecl *fn_lua_pushnumber)(lua_State* L, lua_Number n);
typedef void       (__cdecl *fn_lua_pushstring)(lua_State* L, const char* s);
typedef void       (__cdecl *fn_lua_pushboolean)(lua_State* L, int b);
typedef void       (__cdecl *fn_lua_pushnil)(lua_State* L);
typedef int        (__cdecl *fn_lua_toboolean)(lua_State* L, int index);

// We still need API calls for pushing strings safely (interning).
static fn_lua_tolstring   lua_tolstring_  = (fn_lua_tolstring)0x0084E0E0;
static fn_lua_tonumber    lua_tonumber_   = (fn_lua_tonumber)0x0084E030;
static fn_lua_gettop      lua_gettop_     = (fn_lua_gettop)0x0084DBD0;
static fn_lua_type        lua_type_       = (fn_lua_type)0x0084DEB0;
static fn_lua_pushnumber  lua_pushnumber_ = (fn_lua_pushnumber)0x0084E2A0;
static fn_lua_pushstring  lua_pushstring_ = (fn_lua_pushstring)0x0084E350;
static fn_lua_pushboolean lua_pushboolean_= (fn_lua_pushboolean)0x0084E4D0;
static fn_lua_pushnil     lua_pushnil_    = (fn_lua_pushnil)0x0084E280;
static fn_lua_toboolean   lua_toboolean_  = (fn_lua_toboolean)0x0084E0B0;

typedef void (__cdecl *fn_lua_pushcclosure)(lua_State* L, ScriptFunc_fn fn, int n);
typedef void (__cdecl *fn_lua_setfield)(lua_State* L, int idx, const char* name);

static fn_lua_pushcclosure lua_pushcclosure_ = (fn_lua_pushcclosure)0x0084E400;
static fn_lua_setfield     lua_setfield_     = (fn_lua_setfield)0x0084E900;

#define LUA_TNIL     0
#define LUA_TBOOLEAN 1
#define LUA_TNUMBER  3
#define LUA_TSTRING  4

static constexpr uintptr_t ADDR_GetItemInfo  = 0x00516C60;
static constexpr uintptr_t ADDR_GetSpellInfo = 0x00540A30;

static ScriptFunc_fn orig_GetItemInfo  = (ScriptFunc_fn)0x00516C60;
static ScriptFunc_fn orig_GetSpellInfo = (ScriptFunc_fn)0x00540A30;

// ================================================================
// Cache structures - 8192 slots per cache, direct-mapped.
// ================================================================

static constexpr int CACHE_SIZE    = 8192;
static constexpr int CACHE_MASK    = CACHE_SIZE - 1;
static constexpr int ITEM_RETVALS  = 16;  // Support up to 16 return values (LAA/HD safe)
static constexpr int SPELL_RETVALS = 16;  // Support up to 16 return values (LAA/HD safe)

struct CachedRetVal {
    int    type;
    double numVal;
    char   strVal[512];
};

struct SpellCacheEntry {
    uint32_t     keyHash;
    bool         valid;
    int          retCount;
    int          pushed;
    
    // Key 1 details
    int          keyType1;
    double       keyNum1;
    char         keyStr1[128];
    
    // Key 2 details
    int          keyType2;
    double       keyNum2;
    char         keyStr2[64];

    CachedRetVal vals[SPELL_RETVALS];
};

struct ItemCacheEntry {
    uint32_t     keyHash;
    bool         valid;
    int          retCount;
    int          pushed;

    // Key 1 details
    int          keyType1;
    double       keyNum1;
    char         keyStr1[256];

    CachedRetVal vals[ITEM_RETVALS];
};

static ItemCacheEntry  g_itemCache[CACHE_SIZE]  = {};
static SpellCacheEntry g_spellCache[CACHE_SIZE] = {};

static long g_itemHits    = 0;
static long g_itemMisses  = 0;
static long g_spellHits   = 0;
static long g_spellMisses = 0;
static bool g_active      = false;

static SRWLOCK g_itemCacheLock = SRWLOCK_INIT;
static SRWLOCK g_spellCacheLock = SRWLOCK_INIT;

void ClearCache();

// ================================================================
// FNV-1a Hash - limited length for long item links.
// ================================================================

static inline uint32_t HashStr(const char* s, size_t max_len) {
    uint32_t h = 0x811C9DC5;
    size_t len = 0;
    while (*s && len < max_len) {
        h ^= (uint8_t)*s++;
        h *= 0x01000193;
        len++;
    }
    return h;
}

static inline uint32_t ComputeItemHash(lua_State* L, RawTValue* base, int nargs,
                                      int& kType1, double& kNum1, char* kStr1) {
    kType1 = LUA_TNIL; kNum1 = 0.0; kStr1[0] = '\0';
    if (nargs < 1) return 0;

    uint32_t h = 0x811C9DC5;
    RawTValue* arg1 = &base[0];
    kType1 = arg1->tt;
    h ^= kType1;
    h *= 0x01000193;

    if (kType1 == LUA_TNUMBER) {
        kNum1 = ReadTNumberDirect(arg1);
        uint64_t u;
        memcpy(&u, &kNum1, sizeof(u));
        h ^= (uint32_t)u; h *= 0x01000193;
        h ^= (uint32_t)(u >> 32); h *= 0x01000193;
    } else if (kType1 == LUA_TSTRING) {
        size_t len = 0;
        const char* s = ReadTStringDirect(arg1, &len);
        if (s) {
            size_t copylen = len > 255 ? 255 : len;
            memcpy(kStr1, s, copylen);
            kStr1[copylen] = '\0';
            h ^= HashStr(kStr1, copylen);
            h *= 0x01000193;
        }
    }
    return h;
}

static inline uint32_t ComputeSpellHash(lua_State* L, RawTValue* base, int nargs,
                                       int& kType1, double& kNum1, char* kStr1,
                                       int& kType2, double& kNum2, char* kStr2) {
    kType1 = LUA_TNIL; kNum1 = 0.0; kStr1[0] = '\0';
    kType2 = LUA_TNIL; kNum2 = 0.0; kStr2[0] = '\0';
    if (nargs < 1) return 0;

    uint32_t h = 0x811C9DC5;

    // Process arg1
    RawTValue* arg1 = &base[0];
    kType1 = arg1->tt;
    h ^= kType1;
    h *= 0x01000193;

    if (kType1 == LUA_TNUMBER) {
        kNum1 = ReadTNumberDirect(arg1);
        uint64_t u;
        memcpy(&u, &kNum1, sizeof(u));
        h ^= (uint32_t)u; h *= 0x01000193;
        h ^= (uint32_t)(u >> 32); h *= 0x01000193;
    } else if (kType1 == LUA_TSTRING) {
        size_t len = 0;
        const char* s = ReadTStringDirect(arg1, &len);
        if (s) {
            size_t copylen = len > 127 ? 127 : len;
            memcpy(kStr1, s, copylen);
            kStr1[copylen] = '\0';
            h ^= HashStr(kStr1, copylen);
            h *= 0x01000193;
        }
    }

    // Process arg2
    if (nargs >= 2) {
        RawTValue* arg2 = &base[1];
        kType2 = arg2->tt;
        h ^= kType2;
        h *= 0x01000193;
        if (kType2 == LUA_TNUMBER) {
            kNum2 = ReadTNumberDirect(arg2);
            uint64_t u;
            memcpy(&u, &kNum2, sizeof(u));
            h ^= (uint32_t)u; h *= 0x01000193;
            h ^= (uint32_t)(u >> 32); h *= 0x01000193;
        } else if (kType2 == LUA_TSTRING) {
            size_t len = 0;
            const char* s = ReadTStringDirect(arg2, &len);
            if (s) {
                size_t copylen = len > 63 ? 63 : len;
                memcpy(kStr2, s, copylen);
                kStr2[copylen] = '\0';
                h ^= HashStr(kStr2, copylen);
                h *= 0x01000193;
            }
        }
    }
    return h;
}

// ================================================================
// Direct Memory Capture - reads return values from stack
// using TValue* pointer math, NO lua API calls.
// ================================================================

static void CaptureItemReturnValues(lua_State* L, ItemCacheEntry* e,
                                     uint32_t keyHash, int topBefore, int pushed,
                                     int kType1, double kNum1, const char* kStr1) {
    if (pushed > ITEM_RETVALS) pushed = ITEM_RETVALS;
    e->keyHash   = keyHash;
    e->valid     = true;
    e->retCount  = pushed;  // Approximation, matches actual return count
    e->pushed    = pushed;
    
    e->keyType1  = kType1;
    e->keyNum1   = kNum1;
    memcpy(e->keyStr1, kStr1, sizeof(e->keyStr1));

    RawTValue* base = GetStackBase(L);

    for (int i = 0; i < pushed; i++) {
        RawTValue* val = &base[topBefore + i];
        int t = val->tt;

        e->vals[i].type      = t;
        e->vals[i].numVal    = 0.0;
        e->vals[i].strVal[0] = '\0';

        switch (t) {
            case LUA_TSTRING: {
                size_t slen = 0;
                const char* s = ReadTStringDirect(val, &slen);
                if (s && slen < sizeof(e->vals[i].strVal)) {
                    memcpy(e->vals[i].strVal, s, slen);
                    e->vals[i].strVal[slen] = '\0';
                } else {
                    e->vals[i].type = LUA_TNIL;  // Too long or invalid
                }
                break;
            }
            case LUA_TNUMBER:
                e->vals[i].numVal = ReadTNumberDirect(val);
                break;
            case LUA_TBOOLEAN:
                e->vals[i].numVal = (val->value.gc != NULL) ? 1.0 : 0.0;
                break;
            default:
                e->vals[i].type = LUA_TNIL;
                break;
        }
    }
}

static void CaptureSpellReturnValues(lua_State* L, SpellCacheEntry* e,
                                      uint32_t keyHash, int topBefore, int pushed,
                                      int kType1, double kNum1, const char* kStr1,
                                      int kType2, double kNum2, const char* kStr2) {
    if (pushed > SPELL_RETVALS) pushed = SPELL_RETVALS;
    e->keyHash   = keyHash;
    e->valid     = true;
    e->retCount  = pushed;
    e->pushed    = pushed;

    e->keyType1  = kType1;
    e->keyNum1   = kNum1;
    memcpy(e->keyStr1, kStr1, sizeof(e->keyStr1));

    e->keyType2  = kType2;
    e->keyNum2   = kNum2;
    memcpy(e->keyStr2, kStr2, sizeof(e->keyStr2));

    RawTValue* base = GetStackBase(L);

    for (int i = 0; i < pushed; i++) {
        RawTValue* val = &base[topBefore + i];
        int t = val->tt;

        e->vals[i].type      = t;
        e->vals[i].numVal    = 0.0;
        e->vals[i].strVal[0] = '\0';

        switch (t) {
            case LUA_TSTRING: {
                size_t slen = 0;
                const char* s = ReadTStringDirect(val, &slen);
                if (s && slen < sizeof(e->vals[i].strVal)) {
                    memcpy(e->vals[i].strVal, s, slen);
                    e->vals[i].strVal[slen] = '\0';
                } else {
                    e->vals[i].type = LUA_TNIL;
                }
                break;
            }
            case LUA_TNUMBER:
                e->vals[i].numVal = ReadTNumberDirect(val);
                break;
            case LUA_TBOOLEAN:
                e->vals[i].numVal = (val->value.gc != NULL) ? 1.0 : 0.0;
                break;
            default:
                e->vals[i].type = LUA_TNIL;
                break;
        }
    }
}

// ================================================================
// Replay - uses API calls to safely push values (string interning).
// ================================================================

static inline void ReplayItemCachedValues(lua_State* L, ItemCacheEntry* e) {
    for (int i = 0; i < e->pushed; i++) {
        switch (e->vals[i].type) {
            case LUA_TSTRING:  lua_pushstring_(L, e->vals[i].strVal);       break;
            case LUA_TNUMBER:  lua_pushnumber_(L, e->vals[i].numVal);       break;
            case LUA_TBOOLEAN: lua_pushboolean_(L, (int)e->vals[i].numVal); break;
            default:           lua_pushnil_(L);                              break;
        }
    }
}

static inline void ReplaySpellCachedValues(lua_State* L, SpellCacheEntry* e) {
    for (int i = 0; i < e->pushed; i++) {
        switch (e->vals[i].type) {
            case LUA_TSTRING:  lua_pushstring_(L, e->vals[i].strVal);       break;
            case LUA_TNUMBER:  lua_pushnumber_(L, e->vals[i].numVal);       break;
            case LUA_TBOOLEAN: lua_pushboolean_(L, (int)e->vals[i].numVal); break;
            default:           lua_pushnil_(L);                              break;
        }
    }
}

static lua_State* g_cacheLuaState = nullptr;

// ================================================================
// Hooked_GetItemInfo - Direct Memory Access version.
// ================================================================

static int __cdecl Hooked_GetItemInfo(lua_State* L) {
    if (L != g_cacheLuaState) {
        g_cacheLuaState = L;
        ApiCache::ClearCache();
    }

    int nargs = lua_gettop_(L);
    if (nargs < 1) return orig_GetItemInfo(L);

    RawTValue* base = GetStackBase(L);
    int keyType1;
    double keyNum1;
    char keyStr1[256];
    
    uint32_t keyHash = ComputeItemHash(L, base, nargs, keyType1, keyNum1, keyStr1);
    if (keyType1 == LUA_TNIL) return orig_GetItemInfo(L);

    int slot = keyHash & CACHE_MASK;
    ItemCacheEntry* e = &g_itemCache[slot];

    ItemCacheEntry localEntry;
    bool found = false;

    AcquireSRWLockShared(&g_itemCacheLock);
    if (e->valid && e->keyHash == keyHash && e->keyType1 == keyType1) {
        bool match = true;
        if (keyType1 == LUA_TNUMBER) {
            if (e->keyNum1 != keyNum1) match = false;
        } else if (keyType1 == LUA_TSTRING) {
            if (strcmp(e->keyStr1, keyStr1) != 0) match = false;
        }
        if (match) {
            localEntry = *e;
            found = true;
        }
    }
    ReleaseSRWLockShared(&g_itemCacheLock);

    if (found) {
        ReplayItemCachedValues(L, &localEntry);
        InterlockedIncrement(&g_itemHits);
        return localEntry.retCount;
    }

    if (keyType1 == LUA_TNUMBER && keyNum1 > 0) {
        unsigned int itemId = (unsigned int)keyNum1;
        ItemDataPrefetch::PrefetchItem(itemId + 1);
        ItemDataPrefetch::PrefetchItem(itemId + 2);
    }

    int topBefore = lua_gettop_(L);
    int ret = orig_GetItemInfo(L);
    int topAfter = lua_gettop_(L);
    int pushed = topAfter - topBefore;

    // Only cache successful results with item name + type (first two returns are strings)
    if (pushed >= 10 && pushed <= ITEM_RETVALS) {
        RawTValue* base = GetStackBase(L); // Re-fetch in case stack reallocated
        RawTValue* res1 = &base[topBefore];
        RawTValue* res2 = &base[topBefore + 1];
        if (res1->tt == LUA_TSTRING && res2->tt == LUA_TSTRING) {
            RawTValue* res4 = &base[topBefore + 3];
            bool isLoaded = (res4->tt == LUA_TNUMBER && ReadTNumberDirect(res4) >= 1.0);
            if (isLoaded) {
                AcquireSRWLockExclusive(&g_itemCacheLock);
                CaptureItemReturnValues(L, e, keyHash, topBefore, pushed, keyType1, keyNum1, keyStr1);
                ReleaseSRWLockExclusive(&g_itemCacheLock);
            }
        }
    }

    InterlockedIncrement(&g_itemMisses);
    return ret;
}

// ================================================================
// Hooked_GetSpellInfo - Direct Memory Access version.
// ================================================================

static int __cdecl Hooked_GetSpellInfo(lua_State* L) {
    if (L != g_cacheLuaState) {
        g_cacheLuaState = L;
        ApiCache::ClearCache();
    }

    int nargs = lua_gettop_(L);
    if (nargs < 1) return orig_GetSpellInfo(L);

    RawTValue* base = GetStackBase(L);
    int keyType1, keyType2;
    double keyNum1, keyNum2;
    char keyStr1[128], keyStr2[64];

    uint32_t keyHash = ComputeSpellHash(L, base, nargs, keyType1, keyNum1, keyStr1, keyType2, keyNum2, keyStr2);
    if (keyType1 == LUA_TNIL) return orig_GetSpellInfo(L);

    // Bypass caching for spellbook slot queries (slot number, book type)
    if (keyType1 == LUA_TNUMBER && nargs >= 2) {
        InterlockedIncrement(&g_spellMisses);
        return orig_GetSpellInfo(L);
    }

    int slot = keyHash & CACHE_MASK;
    SpellCacheEntry* e = &g_spellCache[slot];

    SpellCacheEntry localEntry;
    bool found = false;

    AcquireSRWLockShared(&g_spellCacheLock);
    if (e->valid && e->keyHash == keyHash &&
        e->keyType1 == keyType1 && e->keyType2 == keyType2) {
        
        bool match = true;
        if (keyType1 == LUA_TNUMBER) {
            if (e->keyNum1 != keyNum1) match = false;
        } else if (keyType1 == LUA_TSTRING) {
            if (strcmp(e->keyStr1, keyStr1) != 0) match = false;
        }

        if (match && keyType2 == LUA_TNUMBER) {
            if (e->keyNum2 != keyNum2) match = false;
        } else if (match && keyType2 == LUA_TSTRING) {
            if (strcmp(e->keyStr2, keyStr2) != 0) match = false;
        }

        if (match) {
            localEntry = *e;
            found = true;
        }
    }
    ReleaseSRWLockShared(&g_spellCacheLock);

    if (found) {
        ReplaySpellCachedValues(L, &localEntry);
        InterlockedIncrement(&g_spellHits);
        return localEntry.retCount;
    }

    int topBefore = lua_gettop_(L);
    int ret = orig_GetSpellInfo(L);
    int topAfter = lua_gettop_(L);
    int pushed = topAfter - topBefore;

    // Only cache successful results (first return is spell name string)
    if (pushed >= 3 && pushed <= SPELL_RETVALS) {
        RawTValue* base = GetStackBase(L); // Re-fetch in case stack reallocated
        RawTValue* res1 = &base[topBefore];
        if (res1->tt == LUA_TSTRING) {
            AcquireSRWLockExclusive(&g_spellCacheLock);
            CaptureSpellReturnValues(L, e, keyHash, topBefore, pushed, keyType1, keyNum1, keyStr1, keyType2, keyNum2, keyStr2);
            ReleaseSRWLockExclusive(&g_spellCacheLock);
        }
    }

    InterlockedIncrement(&g_spellMisses);
    return ret;
}

// ================================================================
// Hook installation helper.
// ================================================================

static bool HookFunc(const char* name, uintptr_t addr, void* hookFn, void** origFn) {
    MH_STATUS s = MH_CreateHook((void*)addr, hookFn, origFn);
    if (s != MH_OK) {
        Log("[ApiCache]   %-25s MH_CreateHook failed (%d)", name, (int)s);
        return false;
    }
    s = MH_EnableHook((void*)addr);
    if (s != MH_OK) {
        Log("[ApiCache]   %-25s MH_EnableHook failed (%d)", name, (int)s);
        return false;
    }
    Log("[ApiCache]   %-25s 0x%08X  [ OK ]", name, (unsigned)addr);
    return true;
}

// ================================================================
// Public API.
// ================================================================

namespace ApiCache {

bool Init() {
    g_active = true;

    int hooked = 0;
#if !TEST_DISABLE_GETITEMINFO_CACHE
    if (HookFunc("GetItemInfo", ADDR_GetItemInfo, (void*)Hooked_GetItemInfo, (void**)&orig_GetItemInfo))
        hooked++;
#endif

#if !TEST_DISABLE_GETSPELLINFO_CACHE
    if (HookFunc("GetSpellInfo", ADDR_GetSpellInfo, (void*)Hooked_GetSpellInfo, (void**)&orig_GetSpellInfo))
        hooked++;
#endif

    Log("[ApiCache] Init complete: %d/2 API cache hooks active", hooked);
    return true;
}

void Shutdown() {
    if (!g_active) return;
    g_active = false;

    MH_DisableHook((void*)ADDR_GetItemInfo);
    MH_DisableHook((void*)ADDR_GetSpellInfo);

    long itemTotal  = g_itemHits  + g_itemMisses;
    long spellTotal = g_spellHits + g_spellMisses;

    if (itemTotal > 0) {
        Log("[ApiCache] GetItemInfo: %ld hits, %ld misses (%.1f%% hit rate)",
            g_itemHits, g_itemMisses, (double)g_itemHits / itemTotal * 100.0);
    }

    if (spellTotal > 0) {
        Log("[ApiCache] GetSpellInfo: %ld hits, %ld misses (%.1f%% hit rate)",
            g_spellHits, g_spellMisses, (double)g_spellHits / spellTotal * 100.0);
    }
}

void ClearCache() {
    AcquireSRWLockExclusive(&g_itemCacheLock);
    memset(g_itemCache,  0, sizeof(g_itemCache));
    ReleaseSRWLockExclusive(&g_itemCacheLock);

    AcquireSRWLockExclusive(&g_spellCacheLock);
    memset(g_spellCache, 0, sizeof(g_spellCache));
    ReleaseSRWLockExclusive(&g_spellCacheLock);

    Log("[ApiCache] Cache cleared (item: %d entries, spell: %d entries)", CACHE_SIZE, CACHE_SIZE);
}

Stats GetStats() {
    Stats s;
    s.itemHits   = g_itemHits;
    s.itemMisses = g_itemMisses;
    s.spellHits  = g_spellHits;
    s.spellMisses = g_spellMisses;
    s.active     = g_active;
    return s;
}

} // namespace ApiCache
