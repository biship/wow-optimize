#pragma once

// ============================================================================
// Module: wow_memory_opt.h
// Description: Installs and manages target intercepts for subsystem `wow_memory_opt.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================









#ifndef WOW_MEMORY_OPT_H
#define WOW_MEMORY_OPT_H

namespace WowMemoryOpt {
    // Call early in DllMain or MainThread before any allocations
    bool EnableLargeAddressAware();
    
    // Initialize async worker thread pool for MPQ/texture/model loading
    bool InitAsyncWorkerPool();
    void ShutdownAsyncWorkerPool();
    
    // Apply aggressive memory optimizations within 32-bit limits
    bool ApplyMemoryOptimizations();
    
    // Stats
    void DumpStats();
}

#endif
