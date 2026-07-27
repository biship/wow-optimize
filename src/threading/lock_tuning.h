#pragma once

// ============================================================================
// Module: lock_tuning.h
// ============================================================================










// Reduces lock-contention stalls by giving WoW's critical sections a userspace
// spin count. WoW's static MSVC CRT created its locks (heap, stdio, errno, ...)
// with InitializeCriticalSection -- spin count 0 -- so every contended acquisition
// is a kernel wait + context switch. On a many-core CPU a brief spin is far
// cheaper. Semantics are unchanged; only the spin-before-block behaviour differs.
bool InstallLockTuning();
void GetLockTuningStats(unsigned* retrofitted, unsigned* runtimeTuned);
