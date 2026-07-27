#pragma once

// ============================================================================
// Module: strcat_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `strcat_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









#ifndef STRCAT_FAST_H
#define STRCAT_FAST_H

#include <cstdint>

bool InstallStrcatFast();
void StrcpyFast_GetStats(uint64_t* fast, uint64_t* fallback);

#endif // STRCAT_FAST_H
