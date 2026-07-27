#pragma once

// ============================================================================
// Module: crt_memcpy_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `crt_memcpy_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================


bool InstallMemcpyFast();
void UninstallMemcpyFast();


