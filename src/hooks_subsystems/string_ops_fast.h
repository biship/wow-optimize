#pragma once

// ============================================================================
// Module: string_ops_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `string_ops_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================









#ifndef STRING_OPS_FAST_H
#define STRING_OPS_FAST_H

bool InitStringOpsFast();
void ShutdownStringOpsFast();
void DumpStringOpsStats();

#endif // STRING_OPS_FAST_H
