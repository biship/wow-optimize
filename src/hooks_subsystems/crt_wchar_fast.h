#pragma once

// ============================================================================
// Module: crt_wchar_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `crt_wchar_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









bool InstallCrtWcharSSE2();
void ShutdownCrtWcharSSE2();