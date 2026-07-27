#pragma once

// ============================================================================
// Module: stream_cache.h
// Description: SSE2 vectorized replacement for legacy CRT function `stream_cache.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================










#include <cstdint>

bool InstallStreamCache();
void UninstallStreamCache();
void GetStreamCacheStats(uint64_t* rHits, uint64_t* rTotal, uint64_t* wHits, uint64_t* wTotal);
