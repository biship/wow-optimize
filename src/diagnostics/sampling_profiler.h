#pragma once

// ============================================================================
// Module: sampling_profiler.h
// ============================================================================










// ================================================================
// Sampling Profiler — lightweight in-DLL EIP sampler
// ================================================================
// A background thread samples the main thread's EIP every ~1ms via
// SuspendThread/GetThreadContext/ResumeThread, buckets each sample
// by the nearest known function, and dumps the top-N hot functions
// to the log on shutdown.
//
// This fixes the project's core blind spot: xrefs ≠ runtime frequency.
// Every future optimization becomes data-driven instead of a guess.
//
// Read-only sampling — no hooks into WoW code, no writes to WoW memory.
// Risk class: very low (same API family as crash dumpers use).
// ================================================================

#include <windows.h>
#include <cstdint>

namespace SamplingProfiler {

// Initialize the profiler. Stores the main thread handle for sampling.
// Call after the main thread ID is known (post-injection delay).
bool Init(HANDLE mainThread);

// Stop the sampling thread and dump results to the log.
// Call during DLL shutdown before closing handles.
void Shutdown();

// Returns true if the profiler is actively sampling.
bool IsActive();

// Get total number of samples collected (for diagnostics).
uint64_t GetSampleCount();

// Dump the current top-50 hot functions to the log without stopping sampling.
// Called from the periodic stats dump so the profile is captured even when the
// fast process-exit path skips Shutdown().
void DumpNow();

} // namespace SamplingProfiler