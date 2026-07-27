#pragma once

// ============================================================================
// Module: crt_string_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `crt_string_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================










bool InstallCrtStringFastPaths();
void UninstallCrtStringFastPaths();
