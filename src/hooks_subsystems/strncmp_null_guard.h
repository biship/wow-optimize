#pragma once

// ============================================================================
// Module: strncmp_null_guard.h
// Description: SSE2 vectorized replacement for legacy CRT function `strncmp_null_guard.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









bool InstallStrncmpNullGuard();
