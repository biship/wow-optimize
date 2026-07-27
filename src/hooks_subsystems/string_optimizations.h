#pragma once

// ============================================================================
// Module: string_optimizations.h
// Description: SSE2 vectorized replacement for legacy CRT function `string_optimizations.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================










bool InstallStringOptimizations();
void UninstallStringOptimizations();
