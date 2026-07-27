// ============================================================================
// Module: heap_compactor.cpp
// ============================================================================

#include "version.h"

#if TEST_DISABLE_HEAP_COMPACTOR == 0

#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <atomic>

#pragma comment(lib, "psapi.lib")

// Configuration
static constexpr DWORD MONITOR_INTERVAL_MS = 10000;     // Check every 10s (was 3 - reduce CPU overhead)
static constexpr DWORD LOADING_INTERVAL_MS = 3000;      // Faster checks during loading screens
static constexpr SIZE_T CRITICAL_THRESHOLD = 16 * 1024 * 1024;  // 16MB
static constexpr SIZE_T WARNING_THRESHOLD = 32 * 1024 * 1024;  // 32MB

// Statistics
static std::atomic<uint64_t> g_checksPerformed{0};
static std::atomic<uint64_t> g_compactionsTriggered{0};
static std::atomic<SIZE_T>   g_lastLargestBlock{0};
static std::atomic<SIZE_T>   g_minLargestBlock{SIZE_MAX};
static std::atomic<SIZE_T>   g_maxLargestBlock{0};

// Monitor thread handle
static HANDLE g_monitorThread = nullptr;
static volatile bool g_shutdown = false;

// The monitor thread only ever *requests* compaction; the actual HeapCompact()/
// mi_collect() work runs on the main thread via HeapCompactor_RunPendingWork(),
// called from the existing per-frame maintenance tick. HeapCompact() walks and
// mutates every process heap, and mi_collect(true) can decommit/unmap memory —
// doing that from an unsynchronized background thread raced against whatever
// heap WoW's networking code (or Winsock internally) was using mid-connect,
// which could yank a buffer out from under an in-flight WSA operation and
// surface as "unable to connect" (GitHub issue #39).
static std::atomic<int> g_pendingWork{0}; // 0=none, 1=proactive mi_collect, 2=full ForceHeapCompaction

// Forward declarations
extern "C" void Log(const char* fmt, ...);
extern "C" void mi_collect(bool force);
#include <mimalloc.h>
#include "crash_dumper.h"

// Get largest free virtual memory block
static SIZE_T GetLargestFreeBlock() {
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T largestFree = 0;
    SIZE_T currentFree = 0;
    uintptr_t addr = 0;
    
    while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_FREE) {
            currentFree += mbi.RegionSize;
        } else {
            if (currentFree > largestFree) {
                largestFree = currentFree;
            }
            currentFree = 0;
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (addr < (uintptr_t)mbi.BaseAddress) break; // Overflow
    }
    
    if (currentFree > largestFree) {
        largestFree = currentFree;
    }
    
    return largestFree;
}

// Force heap compaction via Windows heap APIs
static void ForceHeapCompaction() {
    // Get all process heaps
    HANDLE heaps[64];
    DWORD heapCount = GetProcessHeaps(64, heaps);
    
    for (DWORD i = 0; i < heapCount; i++) {
        // HeapCompact is available on Vista+
        // This consolidates free blocks within the heap
        HeapCompact(heaps[i], 0);
    }
    
    // Also trigger mimalloc collection
    // (mimalloc is our global allocator replacement)
    {
        StallProbe probe("ForceHeapCompaction mi_collect", 4.0);
        mi_collect(true);
    }
}

// Forward declaration for loading mode check
namespace LuaOpt { bool IsLoadingMode(); }

// Monitor thread - checks VA state periodically
static DWORD WINAPI MonitorThread(LPVOID) {
    Log("[HeapCompactor] Monitor thread started (interval=%dms, loading=%dms, critical=%dMB)",
        MONITOR_INTERVAL_MS, LOADING_INTERVAL_MS, CRITICAL_THRESHOLD / (1024*1024));
    
    while (!g_shutdown) {
        // Use faster interval during loading screens, slower during gameplay
        DWORD interval = LuaOpt::IsLoadingMode() ? LOADING_INTERVAL_MS : MONITOR_INTERVAL_MS;
        Sleep(interval);
        
        SIZE_T largestFree = GetLargestFreeBlock();
        g_checksPerformed++;
        
        // Update statistics
        g_lastLargestBlock = largestFree;
        
        SIZE_T minVal = g_minLargestBlock.load();
        while (largestFree < minVal && 
               !g_minLargestBlock.compare_exchange_weak(minVal, largestFree));
        
        SIZE_T maxVal = g_maxLargestBlock.load();
        while (largestFree > maxVal && 
               !g_maxLargestBlock.compare_exchange_weak(maxVal, largestFree));
        
        // Check thresholds — only *request* compaction here; the main thread
        // performs the actual heap mutation (see HeapCompactor_RunPendingWork).
        if (largestFree < CRITICAL_THRESHOLD) {
            // Rate-limited: this fires every monitor tick once the process is out
            // of address space, and a tester log shows it repeating unchanged for
            // five minutes. One line per 30s is enough to establish the state.
            static DWORD lastCriticalTick = 0;
            DWORD nowTick = GetTickCount();
            if (nowTick - lastCriticalTick > 30000) {
                Log("[HeapCompactor] CRITICAL: LargestFreeBlock=%uMB (<%dMB) - requesting compaction",
                    (unsigned)(largestFree / (1024*1024)), (int)(CRITICAL_THRESHOLD / (1024*1024)));
                lastCriticalTick = nowTick;
            }
            g_pendingWork.store(2, std::memory_order_release);
        } else if (largestFree < WARNING_THRESHOLD) {
            // Proactively compact before reaching critical threshold
            static DWORD lastWarningTick = 0;
            DWORD now = GetTickCount();
            if (now - lastWarningTick > 30000) { // Max 1 per 30 seconds
                Log("[HeapCompactor] WARNING: LargestFreeBlock=%uMB (<32MB) - requesting proactive compaction",
                    (unsigned)(largestFree / (1024*1024)));
                int expected = 0;
                g_pendingWork.compare_exchange_strong(expected, 1, std::memory_order_release);
                lastWarningTick = now;
            }
        }
    }

    Log("[HeapCompactor] Monitor thread shutting down");
    return 0;
}

// Called once per frame from the main thread's existing periodic maintenance
// tick. Performs whatever compaction the monitor thread requested — keeps all
// heap-mutating work off the background thread.
extern "C" void HeapCompactor_RunPendingWork() {
    int work = g_pendingWork.exchange(0, std::memory_order_acquire);
    if (work == 0) return;

    if (work == 2) {
        SIZE_T before = GetLargestFreeBlock();

        // mimalloc runs with purge_decommits off, so a purge resets pages but
        // leaves them committed - physical RAM comes back, address space does not.
        // That is the right trade in normal play (the comment at the option's
        // setting explains the driver-fault reasoning), and exactly the wrong one
        // here: this branch only runs because the process has no address space
        // left, which is the one resource resetting cannot return.
        //
        // A tester's session climbed to 3533 MB of mimalloc commit with 223 MB of
        // VA free and a 1 MB largest block, and every compaction pass achieved
        // nothing. Decommit for the duration of this pass only, then restore.
        {
            StallProbe probe("critical mi_collect (decommit)", 4.0);
            mi_option_set(mi_option_purge_decommits, 1);
            mi_collect(true);
            mi_option_set(mi_option_purge_decommits, 0);
        }

        ForceHeapCompaction();
        g_compactionsTriggered++;
        SIZE_T after = GetLargestFreeBlock();
        Log("[HeapCompactor] After compaction: LargestFreeBlock=%uMB (%+dMB)",
            (unsigned)(after / (1024*1024)), (int)((after - before) / (1024*1024)));
    } else {
        {
            StallProbe probe("compaction mi_collect", 4.0);
            mi_collect(true);
        }
        g_compactionsTriggered++;
    }
}

bool HeapCompactor_Init() {
    if (g_monitorThread) {
        Log("[HeapCompactor] Already initialized");
        return true;
    }
    
    g_shutdown = false;
    g_monitorThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    
    if (!g_monitorThread) {
        Log("[HeapCompactor] Failed to create monitor thread");
        return false;
    }
    
    // Log initial state
    SIZE_T initialFree = GetLargestFreeBlock();
    Log("[HeapCompactor] ACTIVE (initial LargestFreeBlock=%uMB)",
        (unsigned)(initialFree / (1024*1024)));
    
    return true;
}

void HeapCompactor_Shutdown() {
    if (!g_monitorThread) return;
    
    g_shutdown = true;
    WaitForSingleObject(g_monitorThread, 3000);
    CloseHandle(g_monitorThread);
    g_monitorThread = nullptr;
    
    Log("[HeapCompactor] Shutdown complete (checks=%llu, compactions=%llu, min=%uMB, max=%uMB)",
        (unsigned long long)g_checksPerformed.load(),
        (unsigned long long)g_compactionsTriggered.load(),
        (unsigned)(g_minLargestBlock.load() / (1024*1024)),
        (unsigned)(g_maxLargestBlock.load() / (1024*1024)));
}

// Query current state (for diagnostics)
extern "C" SIZE_T HeapCompactor_GetLargestFreeBlock() {
    return GetLargestFreeBlock();
}

// Cheap cached read (no VirtualQuery walk) for per-frame consumers like the GC
// step. Returns the last value the monitor sampled; 0 means "not sampled yet".
extern "C" SIZE_T HeapCompactor_GetCachedLargestBlock() {
    return g_lastLargestBlock.load();
}

extern "C" void HeapCompactor_GetStats(uint64_t* checks, uint64_t* compactions, 
                                        SIZE_T* lastBlock, SIZE_T* minBlock, SIZE_T* maxBlock) {
    if (checks) *checks = g_checksPerformed.load();
    if (compactions) *compactions = g_compactionsTriggered.load();
    if (lastBlock) *lastBlock = g_lastLargestBlock.load();
    if (minBlock) *minBlock = g_minLargestBlock.load();
    if (maxBlock) *maxBlock = g_maxLargestBlock.load();
}

#else  // TEST_DISABLE_HEAP_COMPACTOR

// Compactor disabled: report "no data" so VA-pressure consumers stay inert.
#include <windows.h>
extern "C" SIZE_T HeapCompactor_GetCachedLargestBlock() { return 0; }

#endif // TEST_DISABLE_HEAP_COMPACTOR
