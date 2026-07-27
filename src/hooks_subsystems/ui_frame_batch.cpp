// ============================================================================
// Module: ui_frame_batch.cpp
// ============================================================================

#include <windows.h>
#include "MinHook.h"
#include "version.h"
#include "config.h"
#include "../allocators/loading_defrag.h"

extern "C" void Log(const char* fmt, ...);

// Non-namespaced wrapper so the naked asm hook below can `call` it by plain
// symbol name (inline asm can't reference a namespaced C++ function directly).
static int CheckLoadingActive() {
    return LoadingDefrag::IsLoadingActive() ? 1 : 0;
}

// ================================================================
// UI Frame Update Batching
// ================================================================

// Stats
static volatile long g_batchedFrames = 0;      // Number of frame update batches processed
static volatile long g_totalIterations = 0;    // Total frame update iterations
static volatile long g_peakIterations = 0;     // Peak iterations in a single batch

// Global flag that the original function checks (dword_B41834)
static DWORD* g_frameUpdatePending = (DWORD*)0x00B41834;

// Original frame update loop at 0x004800F0
// Signature: int __usercall@<eax>(int@<ebx>, int@<edi>, int)
// This uses a custom calling convention with ebx, edi registers + stack parameter
// We need to use a naked function to preserve the exact calling convention
typedef int (__cdecl* FrameUpdateLoop_fn)();  // Will be called via inline asm
static FrameUpdateLoop_fn orig_FrameUpdateLoop = nullptr;

// ================================================================
// Hooked Frame Update Loop - Naked function to preserve registers
// ================================================================
__declspec(naked) static int Hooked_FrameUpdateLoop() {
#if TEST_DISABLE_UI_FRAME_BATCH
    __asm {
        jmp dword ptr [orig_FrameUpdateLoop]
    }
#else
    __asm {
        // Preserve all registers
        push ebp
        mov ebp, esp
        pushad

        // This dispatcher is shared plumbing used during loading screens and
        // char-select as well as in-world UI (GitHub issue #36: "why is this
        // modifying the loading screen at all"). Skip the stat bookkeeping
        // entirely outside gameplay so it adds zero overhead/interference
        // during those transitions.
        call CheckLoadingActive
        test eax, eax
        jnz skip_counting

        // Read dword_B41834 to count pending updates
        mov eax, dword ptr [0x00B41834]
        mov ecx, eax
        xor edx, edx

        // Count bits in ecx
    count_loop:
        test ecx, ecx
        jz count_done
        mov eax, ecx
        and eax, 1
        add edx, eax
        shr ecx, 1
        jmp count_loop

    count_done:
        // edx now contains iteration count. Single-writer (main thread only,
        // this dispatcher never runs off-thread) so no lock prefix needed —
        // the prior unconditional bus-locked atomics added needless latency
        // on every call through this hot shared path.
        inc dword ptr [g_batchedFrames]
        add dword ptr [g_totalIterations], edx

        mov eax, dword ptr [g_peakIterations]
    peak_loop:
        cmp edx, eax
        jle peak_done
        cmpxchg dword ptr [g_peakIterations], edx
        jne peak_loop

    peak_done:
    skip_counting:
        // Restore registers
        popad
        pop ebp

        // Jump to original function
        jmp dword ptr [orig_FrameUpdateLoop]
    }
#endif
}

// ================================================================
// Installation
// ================================================================
bool InstallUIFrameBatching() {
    if (!Config::g_settings.OptUIFrameBatch) {
        Log("[UIFrameBatch] DISABLED via configuration (wow_opt.ini)");
        return false;
    }
#if TEST_DISABLE_UI_FRAME_BATCH
    Log("[UIFrameBatch] DISABLED (test toggle)");
    return false;
#else
    // Hook the frame update loop at 0x004800F0
    // This function loops while dword_B41834 is set and processes frame updates
    void* targetAddr = (void*)0x004800F0;
    
    if (MH_CreateHook(targetAddr, (void*)Hooked_FrameUpdateLoop, (void**)&orig_FrameUpdateLoop) != MH_OK) {
        Log("[UIFrameBatch] Failed to hook frame update loop");
        return false;
    }
    if (MH_EnableHook(targetAddr) != MH_OK) {
        Log("[UIFrameBatch] Failed to enable frame update loop hook");
        return false;
    }

    Log("[UIFrameBatch] ACTIVE (hooked frame update loop at 0x004800F0)");
    return true;
#endif
}

// ================================================================
// Stats
// ================================================================
void GetUIFrameBatchStats(long* batched, long* individual, long* frames) {
    if (batched) *batched = g_batchedFrames;
    if (individual) *individual = g_totalIterations;
    if (frames) *frames = g_peakIterations;
}

// ================================================================
// Cleanup
// ================================================================
void ShutdownUIFrameBatching() {
    // Nothing to clean up - no dynamic allocations
}
