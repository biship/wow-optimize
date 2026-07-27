#pragma once

// ============================================================================
// Module: crt_free_hook.h
// Description: SSE2 vectorized replacement for legacy CRT function `crt_free_hook.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









#include <cstdint>

bool InstallCrtFreeHook();
void UninstallCrtFreeHook();
void GetCrtFreeStats(uint64_t* hits, uint64_t* total);
