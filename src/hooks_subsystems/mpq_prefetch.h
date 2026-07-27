#pragma once

// ============================================================================
// Module: mpq_prefetch.h
// ============================================================================

#include <windows.h>

namespace MPQPrefetch {

struct Stats {
    long filesQueued;
    long filesCompleted;
    long filesDropped;
    long cacheHits;
    long cacheMisses;
    long queueDepth;
    long zoneTransitions;
    double totalPrefetchTimeMs;
};

bool Init();
void Shutdown();
void OnFrame(DWORD mainThreadId);
void QueuePrefetch(const char* filename);
Stats GetStats();


} // namespace MPQPrefetch
