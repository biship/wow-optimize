#pragma once

// ============================================================================
// Module: crt_char_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `crt_char_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









bool InstallCrtCharSSE2();
void ShutdownCrtCharSSE2();
