#pragma once

// ============================================================================
// Module: spell_cache.h
// ============================================================================










// ================================================================
// Spell Data Caching - Cache spell coefficients, ranges, cooldowns
//
// ================================================================

#include <windows.h>
#include <unordered_map>

namespace SpellCache {

// Spell data entry (simplified - actual structure may vary)
struct SpellData {
    uint32_t spellID;
    float coefficient;
    float range;
    uint32_t cooldown;
    uint32_t castTime;
    DWORD timestamp;      // Last access time (for LRU)
    uint32_t accessCount; // Access counter
};

// Cache statistics
struct Stats {
    long hits;
    long misses;
    long evictions;
    long cacheSize;
};

// Initialize spell cache
bool Init();

// Shutdown and cleanup
void Shutdown();

// Get cache statistics
void GetStats(Stats* stats);

// Clear cache (called on UI reload)
void Clear();

} // namespace SpellCache
