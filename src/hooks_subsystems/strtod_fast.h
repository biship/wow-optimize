#pragma once

// ============================================================================
// Module: strtod_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `strtod_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









bool InstallStrtodFast();
void UninstallStrtodFast();
