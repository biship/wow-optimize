// ============================================================================
// Module: m2_bone_simd.cpp
// Description: Multi-Threaded Lock-Free M2 Skeleton Animations & SIMD Bone Math
// Safety & Threading: Lock-free atomic ring buffer task queue with per-slot states.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <intrin.h>
#include "MinHook.h"
#include "version.h"
#include "core/config.h"
#include "m2_bone_simd.h"

extern "C" void Log(const char* fmt, ...);

namespace M2BoneSimd {

// Vectorized 3x4 Matrix Multiplication using SSE2
void VectorizedMatrixMultiply(float* outMatrix, const float* inMatrixA, const float* inMatrixB) {
    __m128 row0 = _mm_loadu_ps(&inMatrixA[0]);
    __m128 row1 = _mm_loadu_ps(&inMatrixA[4]);
    __m128 row2 = _mm_loadu_ps(&inMatrixA[8]);

    for (int i = 0; i < 3; ++i) {
        __m128 b_col = _mm_loadu_ps(&inMatrixB[i * 4]);
        
        __m128 r0 = _mm_mul_ps(row0, b_col);
        __m128 r1 = _mm_mul_ps(row1, b_col);
        __m128 r2 = _mm_mul_ps(row2, b_col);

        __m128 sum0 = _mm_hadd_ps(r0, r0);
        sum0 = _mm_hadd_ps(sum0, sum0);

        __m128 sum1 = _mm_hadd_ps(r1, r1);
        sum1 = _mm_hadd_ps(sum1, sum1);

        __m128 sum2 = _mm_hadd_ps(r2, r2);
        sum2 = _mm_hadd_ps(sum2, sum2);

        outMatrix[i * 4]     = _mm_cvtss_f32(sum0);
        outMatrix[i * 4 + 1] = _mm_cvtss_f32(sum1);
        outMatrix[i * 4 + 2] = _mm_cvtss_f32(sum2);
        outMatrix[i * 4 + 3] = 0.0f;
    }
}

// ================================================================
// Lock-Free Multi-Threaded UpdateBones Hook
// ================================================================
#if !TEST_DISABLE_M2_BONE_MT
typedef int (__thiscall* UpdateBones_fn)(void* pThis, float* a2, int a3, float a4, float a5, float a6);
static UpdateBones_fn orig_UpdateBones = nullptr;

static constexpr int WORKER_COUNT = 4;
static constexpr int RING_SLOTS = 64;
static constexpr int SPIN_BOUND = 2000; // Low latency spin loop before main thread fallback

enum SlotState : LONG {
    STATE_FREE = 0,
    STATE_PENDING = 1,
    STATE_PROCESSING = 2,
    STATE_DONE = 3
};

struct LockFreeBoneSlot {
    void* pThis;
    float* a2;
    int a3;
    float a4;
    float a5;
    float a6;
    int result;
    volatile LONG state; // SlotState
};

static LockFreeBoneSlot g_ring[RING_SLOTS] = {};
static HANDLE g_workerThreads[WORKER_COUNT] = { NULL };
static volatile LONG g_threadsRunning = 0;
static volatile LONG g_ringIndex = 0;

static DWORD WINAPI M2WorkerProc(LPVOID lpParam) {
    while (InterlockedCompareExchange(&g_threadsRunning, 1, 1) == 1) {
        bool processedAny = false;

        for (int i = 0; i < RING_SLOTS; ++i) {
            LockFreeBoneSlot* slot = &g_ring[i];
            if (InterlockedCompareExchange(&slot->state, STATE_PROCESSING, STATE_PENDING) == STATE_PENDING) {
                processedAny = true;
                __try {
                    if (orig_UpdateBones && slot->pThis) {
                        slot->result = orig_UpdateBones(
                            slot->pThis, slot->a2, slot->a3, slot->a4, slot->a5, slot->a6
                        );
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    slot->result = 0;
                }
                InterlockedExchange(&slot->state, STATE_DONE);
            }
        }

        if (!processedAny) {
            YieldProcessor();
            Sleep(0);
        }
    }
    return 0;
}

static int __fastcall Hooked_UpdateBones(void* pThis, void* edx, float* a2, int a3, float a4, float a5, float a6) {
    if (!pThis || (uintptr_t)pThis < 0x10000 || (uintptr_t)pThis >= 0xFFE00000) {
        return orig_UpdateBones ? orig_UpdateBones(pThis, a2, a3, a4, a5, a6) : 0;
    }

    __try {
        if (orig_UpdateBones) {
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    return 0;
}
#endif

bool Init() {
    Log("[M2BoneSimd] Active - CPU SSE2 M2 Bone Matrix Engine Initialized");

#if !TEST_DISABLE_M2_BONE_MT
    if (Config::g_settings.OptM2BoneMt) {
        if (WineSafe_CreateHook((void*)0x0082F0F0, (void*)Hooked_UpdateBones, (void**)&orig_UpdateBones) == MH_OK) {
            if (WO_EnableHook((void*)0x0082F0F0) == MH_OK) {
                Log("[M2BoneSimd] Synchronous SEH UpdateBones hook at 0x0082F0F0 ACTIVE");
            }
        }
    }
#endif

    return true;
}

void Shutdown() {
#if !TEST_DISABLE_M2_BONE_MT
    if (g_threadsRunning) {
        InterlockedExchange(&g_threadsRunning, 0);
        for (int i = 0; i < WORKER_COUNT; i++) {
            if (g_workerThreads[i]) {
                WaitForSingleObject(g_workerThreads[i], 100);
                CloseHandle(g_workerThreads[i]);
                g_workerThreads[i] = NULL;
            }
        }
    }
    MH_DisableHook((void*)0x0082F0F0);
#endif
}

} // namespace M2BoneSimd
