#pragma once

// ============================================================================
// Module: matrix_copy_sse2.h
// ============================================================================










bool InstallMatrixCopySSE2();
void ShutdownMatrixCopySSE2();
// Fifteen call counters, from the periodic report.
void MatrixCopySSE2_LogStats(void);
