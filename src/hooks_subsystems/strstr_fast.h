#pragma once

// ============================================================================
// Module: strstr_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `strstr_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









bool InstallStrstrSSE2();
void ShutdownStrstrSSE2();
