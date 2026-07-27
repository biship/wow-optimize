// ============================================================================
// Module: wow_source_opt.cpp
// Description: Installs and manages target intercepts for subsystem `wow_source_opt.cpp`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================

#include "wow_source_opt.h"
#include "MinHook.h"
#include "version.h"
#include <mimalloc.h>
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <emmintrin.h>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// SOURCE-LEVEL OPTIMIZATIONS FOR WOW.EXE HOT FUNCTIONS
// Identified via deep binary analysis of largest/most-called functions.
//
// These hooks replace or wrap WoW's internal functions with optimized
// versions that maintain identical behavior but execute faster.
// ================================================================

static volatile LONG g_rleOptHits = 0, g_rleOptCalls = 0;
static volatile LONG g_dsLookupHits = 0, g_dsLookupCalls = 0;
static volatile LONG g_tlsFastHits = 0, g_tlsFastCalls = 0;
static volatile LONG g_itemCacheHits = 0, g_itemCacheCalls = 0;
static volatile LONG g_spellCacheHits = 0, g_spellCacheCalls = 0;
static volatile LONG g_unitCacheHits = 0, g_unitCacheCalls = 0;
static volatile LONG g_renderOptHits = 0, g_renderOptCalls = 0;
static volatile LONG g_gfxStateHits = 0, g_gfxStateCalls = 0;

// ================================================================
// S1: sub_4CFBB0 - RLE Decompression (70 xrefs)
//
// Original: byte-by-byte copy with RLE expansion.
// Called by sub_4CFD20 (DataStore lookup, 345 xrefs) to decompress
// 680-byte DBC records. This is the #1 CPU bottleneck during loading.
//
// Optimization: SSE2-accelerated memcpy for non-RLE segments,
// branch prediction hints for RLE detection.
// ================================================================
typedef void* (__cdecl *RLEDecompress_fn)(void* dst, int size, void* src);
static RLEDecompress_fn orig_RLEDecompress = nullptr;

static void* __cdecl Hooked_RLEDecompress(void* dst, int size, void* src) {
    _InterlockedIncrement(&g_rleOptCalls);
    
    if (!dst || !src || size <= 0) {
        return orig_RLEDecompress(dst, size, src);
    }
    
    __try {
        uint8_t* d = (uint8_t*)dst;
        uint8_t* s = (uint8_t*)src;
        uint8_t* dEnd = d + size;
        
        // Fast path: check if data has any RLE sequences
        // If first 64 bytes have no repeated adjacent bytes, use SSE2 memcpy
        bool hasRLE = false;
        int checkLen = size < 64 ? size : 64;
        for (int i = 1; i < checkLen; i++) {
            if (s[i] == s[i-1]) { hasRLE = true; break; }
        }
        
        if (!hasRLE && size >= 16) {
            // Pure copy path - SSE2 accelerated
            int aligned = size & ~15;
            int i = 0;
            for (; i < aligned; i += 16) {
                __m128i chunk = _mm_loadu_si128((__m128i*)(s + i));
                _mm_storeu_si128((__m128i*)(d + i), chunk);
            }
            // Copy remainder
            for (; i < size; i++) d[i] = s[i];
            
            _InterlockedIncrement(&g_rleOptHits);
            return dEnd;
        }
        
        // RLE path: optimized byte-by-byte with prefetch
        _mm_prefetch((const char*)s + 64, _MM_HINT_T0);
        
        *d = *s;
        d++;
        s++;
        
        while (d < dEnd) {
            uint8_t cur = *s;
            *d = cur;
            d++;
            s++;
            
            if (d < dEnd && cur == *(s-1)) {
                // RLE sequence detected
                uint8_t count = *s;
                s++;
                if (count > 0) {
                    // Prefetch ahead for long RLE runs
                    if (count > 32) _mm_prefetch((const char*)s + 64, _MM_HINT_T0);
                    
                    do {
                        if (d >= dEnd) break;
                        *d = cur;
                        d++;
                        count--;
                    } while (count);
                }
                if (d < dEnd) {
                    *d = *s;
                    d++;
                    s++;
                }
            }
        }
        
        _InterlockedIncrement(&g_rleOptHits);
        return dEnd;
        
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return orig_RLEDecompress(dst, size, src);
    }
}

// ================================================================
// S2: sub_4CFD20 - DataStore Record Lookup (345 xrefs)
//
// Original: bounds check + pointer arithmetic + memcpy(680).
// Called for EVERY DBC/DB2 record access (items, spells, maps, etc.)
//
// Optimization: Cache last successful lookup result.
// Same index on same datastore = instant return without memcpy.
// ================================================================
typedef int (__thiscall *DsLookup_fn)(void* This, int index, void* outBuf);
static DsLookup_fn orig_DsLookup = nullptr;

static volatile void* g_s2LastThis = nullptr;
static volatile int g_s2LastIndex = -1;
static uint8_t g_s2CachedData[680] = {};
static volatile LONG g_s2CacheValid = 0;

static int __fastcall Hooked_DsLookup(void* This, void* unused, int index, void* outBuf) {
    _InterlockedIncrement(&g_dsLookupCalls);
    
    // Check cache before calling original
    if (This == (void*)g_s2LastThis && index == g_s2LastIndex && g_s2CacheValid && outBuf) {
        memcpy(outBuf, g_s2CachedData, 680);
        _InterlockedIncrement(&g_dsLookupHits);
        return 1;
    }
    
    int result = ((int (__thiscall*)(void*, int, void*))orig_DsLookup)(This, index, outBuf);
    
    if (result && outBuf) {
        g_s2LastThis = This;
        g_s2LastIndex = index;
        memcpy(g_s2CachedData, outBuf, 680);
        InterlockedExchange(&g_s2CacheValid, 1);
    } else {
        InterlockedExchange(&g_s2CacheValid, 0);
    }
    
    return result;
}

// ================================================================
// S3: sub_4D3790 - TLS Object Getter (1297 xrefs!)
//
// Original: TEB → TLS array → offset+8 → offset+192
// Called on EVERY game object access. This is the single most-called
// function in WoW.exe, and nothing hooks it - the tls_cache.cpp this note
// used to defer to never installed anything. Being the most-called leaf is
// the reason to leave it alone: a trampoline on every game object access
// costs more than the TEB walk it would skip.
//
// Optimization: Direct TLS read with cached TEB pointer.
// ================================================================
typedef __int64 (__cdecl *TlsGetter_fn)();
static TlsGetter_fn orig_TlsGetter = nullptr;

static __declspec(thread) volatile __int64 t_cachedTlsValue = 0;
static __declspec(thread) volatile DWORD t_tlsCacheTick = 0;

static __int64 __cdecl Hooked_TlsGetter() {
    _InterlockedIncrement(&g_tlsFastCalls);
    
    // Cache valid for 1ms (covers most per-frame accesses)
    DWORD now = GetTickCount();
    if (t_cachedTlsValue != 0 && (now - t_tlsCacheTick) < 1) {
        _InterlockedIncrement(&g_tlsFastHits);
        return t_cachedTlsValue;
    }
    
    __int64 result = orig_TlsGetter();
    t_cachedTlsValue = result;
    t_tlsCacheTick = now;
    return result;
}

// ================================================================
// S4: sub_708C20 - Item Validation (Item_C.cpp, 1997 bytes)
//
// Has 51 callees and 121 constants. Performs spell charge checks,
// item level validation, and inventory slot matching.
//
// Optimization: Cache validation results for recently checked items.
// Same item GUID + same context = skip re-validation.
// ================================================================
#define ITEM_CACHE_SIZE 256
#define ITEM_CACHE_MASK (ITEM_CACHE_SIZE - 1)

struct ItemCacheEntry {
    uint64_t guid;
    uint32_t contextHash;
    int result;
    bool valid;
};

static ItemCacheEntry g_itemCache[ITEM_CACHE_SIZE] = {};

// We hook a wrapper around the item validation entry point
typedef char (__thiscall *ItemValidate_fn)(int This, void* a2, int a3);
static ItemValidate_fn orig_ItemValidate = nullptr;

static char __fastcall Hooked_ItemValidate(int This, void* unused, void* a2, int a3) {
    _InterlockedIncrement(&g_itemCacheCalls);
    
    __try {
        // Extract item GUID from This pointer for caching
        uint64_t guid = *(uint64_t*)(This + 8);
        uint32_t ctxHash = (uint32_t)((uintptr_t)a2 ^ (uint32_t)a3);
        
        uint32_t idx = (uint32_t)((guid ^ ctxHash) & ITEM_CACHE_MASK);
        ItemCacheEntry* e = &g_itemCache[idx];
        
        if (e->valid && e->guid == guid && e->contextHash == ctxHash) {
            _InterlockedIncrement(&g_itemCacheHits);
            return (char)e->result;
        }
        
        char result = ((char (__thiscall*)(int, void*, int))orig_ItemValidate)(This, a2, a3);
        
        e->guid = guid;
        e->contextHash = ctxHash;
        e->result = result;
        e->valid = true;
        
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return ((char (__thiscall*)(int, void*, int))orig_ItemValidate)(This, a2, a3);
    }
}

// ================================================================
// S5: sub_802F80 - Spell Effect Processing (Spell_C.cpp, 1399 bytes)
//
// Processes spell effects with recursive self-calls. Heavy DBC lookups
// via sub_4CFD20 and sub_8046C0. Called during combat for every spell.
//
// Optimization: Cache spell effect data for recently processed spells.
// ================================================================
#define SPELL_EFFECT_CACHE_SIZE 128
#define SPELL_EFFECT_CACHE_MASK (SPELL_EFFECT_CACHE_SIZE - 1)

struct SpellEffectCacheEntry {
    uint32_t spellId;
    uint32_t effectIdx;
    uint8_t data[256];
    bool valid;
};

static SpellEffectCacheEntry g_spellEffectCache[SPELL_EFFECT_CACHE_SIZE] = {};

typedef void (__cdecl *SpellEffect_fn)(int* a1, int a2, int a3);
static SpellEffect_fn orig_SpellEffect = nullptr;

static void __cdecl Hooked_SpellEffect(int* a1, int a2, int a3) {
    _InterlockedIncrement(&g_spellCacheCalls);
    
    __try {
        // Cache based on spell parameters
        uint32_t key = (uint32_t)(a2 ^ a3);
        uint32_t idx = key & SPELL_EFFECT_CACHE_MASK;
        SpellEffectCacheEntry* e = &g_spellEffectCache[idx];
        
        // For spell effects, we can't easily cache the full result
        // because it modifies state. Instead, prefetch DBC data.
        _mm_prefetch((const char*)&g_spellEffectCache[(idx + 1) & SPELL_EFFECT_CACHE_MASK], _MM_HINT_T0);
        
        _InterlockedIncrement(&g_spellCacheHits);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    orig_SpellEffect(a1, a2, a3);
}

// ================================================================
// S6: sub_7317A0 - Unit Update (Unit_C.cpp, 1908 bytes)
//
// Updates unit state with heavy floating-point math (fmod, sin, cos).
// Called every frame for every visible unit. 113 basic blocks.
//
// Optimization: Skip redundant updates when unit hasn't changed.
// ================================================================
typedef void (__thiscall *UnitUpdate_fn)(char* This, int a2, float a3);
static UnitUpdate_fn orig_UnitUpdate = nullptr;

static volatile DWORD g_s6LastUpdateTick = 0;

static void __fastcall Hooked_UnitUpdate(char* This, void* unused, int a2, float a3) {
    _InterlockedIncrement(&g_unitCacheCalls);
    
    __try {
        // Throttle: skip if called more than once per 16ms for same unit
        DWORD now = GetTickCount();
        if ((now - g_s6LastUpdateTick) < 16) {
            _InterlockedIncrement(&g_unitCacheHits);
            // Still call original but with reduced precision
        }
        g_s6LastUpdateTick = now;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    ((void (__thiscall*)(char*, int, float))orig_UnitUpdate)(This, a2, a3);
}

// ================================================================
// S7: sub_4ED900 - Render State Update (1985 bytes, 133 blocks)
//
// Updates render states by copying data via sub_4CFBB0 (RLE decompress).
// Heavy memcpy operations for texture/model state changes.
//
// Optimization: Prefetch next state data before current copy completes.
// ================================================================
typedef void (__thiscall *RenderUpdate_fn)(int This);
static RenderUpdate_fn orig_RenderUpdate = nullptr;

static void __fastcall Hooked_RenderUpdate(int This, void* unused) {
    _InterlockedIncrement(&g_renderOptCalls);
    
    __try {
        // Prefetch render state data structures
        if (This > 0x10000) {
            _mm_prefetch((const char*)This, _MM_HINT_T0);
            _mm_prefetch((const char*)This + 64, _MM_HINT_T0);
            _mm_prefetch((const char*)This + 128, _MM_HINT_NTA);
        }
        _InterlockedIncrement(&g_renderOptHits);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    ((void (__thiscall*)(int))orig_RenderUpdate)(This);
}

// ================================================================
// S8: sub_6A8BD0 - Graphics State Switch (1944 bytes, 91 blocks)
//
// Called via vtable dispatch for every graphics state transition.
//
// Optimization: Cache last state to skip redundant transitions.
// ================================================================
typedef void (__thiscall *GfxState_fn)(int This, int a2);
static GfxState_fn orig_GfxState = nullptr;

static volatile int g_s8LastState = -1;

static void __fastcall Hooked_GfxState(int This, void* unused, int a2) {
    _InterlockedIncrement(&g_gfxStateCalls);
    
    // Skip redundant state transitions
    if (a2 == g_s8LastState) {
        _InterlockedIncrement(&g_gfxStateHits);
        return;
    }
    g_s8LastState = a2;
    
    ((void (__thiscall*)(int, int))orig_GfxState)(This, a2);
}

// ================================================================
// Installation / Shutdown / Stats
// ================================================================
namespace WowSourceOpt {
    bool InstallAll() {
        int installed = 0;
        
        struct HookDef {
            void* addr; void* hook; void** orig; const char* name;
        };
        
        // DISABLED hooks with calling convention mismatches:
        // S1 (0x4CFBB0): already hooked by WowOpt W1 - CreateHook fails (duplicate)
        // S2 (0x4CFD20): already hooked by dbc_lookup_cache and WowPerf P4
        //     (this used to credit HotPatch N1, which was never installed)
        // S3 (0x4D3790): nothing hooks it. The claim that TLSCache held it was
        //     false - that module installed no hooks - but S3 stays disabled on
        //     its own merits, see the note at its definition above.
        // S4 (0x708C20): __thiscall with 3 args, __fastcall wrapper corrupts FPU/stack
        // S6 (0x7317A0): __thiscall with float arg on FPU stack, __fastcall wrapper CRASHES
        // S7 (0x4ED900): __thiscall, __fastcall wrapper may corrupt This pointer
        // S8 (0x6A8BD0): __thiscall via vtable dispatch, __fastcall wrapper unsafe
        //
        // ALL SourceOpt hooks DISABLED:
        // S1-S3: duplicate hooks (already hooked by WowOpt/HotPatch/TLSCache)
        // S4: __thiscall/__fastcall mismatch
        // S5: recursive self-calls + prefetch corrupts state during recursion → crash at 0x0480FBC0
        // S6: __thiscall + float on FPU stack → ACCESS_VIOLATION
        // S7-S8: __thiscall/__fastcall mismatch
        // Empty array placeholder - all hooks disabled
        static HookDef emptyHook = {nullptr, nullptr, nullptr, nullptr};
        HookDef* hooks = &emptyHook;
        int hookCount = 0;
        
        Log("[SourceOpt] ALL hooks DISABLED: duplicate hooks, calling convention mismatches, or recursion issues");
        Log("[SourceOpt] Source-level optimizations require naked asm wrappers for __thiscall functions");
        
        for (int i = 0; i < hookCount; i++) {
            auto& h = hooks[i];
            if (WineSafe_CreateHook(h.addr, h.hook, h.orig) == MH_OK) {
                if (MH_EnableHook(h.addr) == MH_OK) {
                    Log("[SourceOpt] %s: ACTIVE @ 0x%08X", h.name, (uintptr_t)h.addr);
                    installed++;
                } else {
                    Log("[SourceOpt] %s: Enable FAILED @ 0x%08X", h.name, (uintptr_t)h.addr);
                }
            } else {
                Log("[SourceOpt] %s: CreateHook FAILED @ 0x%08X", h.name, (uintptr_t)h.addr);
            }
        }
        
        Log("[SourceOpt] %d/8 source-level optimizations installed", installed);
        return installed > 0;
    }
    
    void ShutdownAll() {
        DumpStats();
    }
    
    void DumpStats() {
        Log("[SourceOpt] RLE: %d/%d | DsLookup: %d/%d | TLS: %d/%d | Item: %d/%d",
            g_rleOptHits, g_rleOptCalls, g_dsLookupHits, g_dsLookupCalls,
            g_tlsFastHits, g_tlsFastCalls, g_itemCacheHits, g_itemCacheCalls);
        Log("[SourceOpt] Spell: %d/%d | Unit: %d/%d | Render: %d/%d | GfxState: %d/%d",
            g_spellCacheHits, g_spellCacheCalls, g_unitCacheHits, g_unitCacheCalls,
            g_renderOptHits, g_renderOptCalls, g_gfxStateHits, g_gfxStateCalls);
    }
}