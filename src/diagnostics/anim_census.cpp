// ============================================================================
// Module: anim_census.cpp
//
// A tester's sampling profile puts roughly a fifth of main-thread execution in
// the M2 animation family around sub_82F0F0 - the largest single target in the
// whole profile, and about twice anything else. That function interpolates the
// translation, rotation and scale tracks of every bone of a model and assembles
// a 4x4 matrix for each, then walks its attachments, particle emitters, ribbon
// emitters and lights.
//
// The obvious thing to do with a fifth of the frame is level-of-detail: stop
// re-animating models that are far away or small on screen every single frame.
// The obvious thing is not currently possible, and this module exists because
// of why.
//
// sub_82F0F0 takes a matrix as its second argument, and a matrix carries a
// translation - so at first glance the model's world position is right there.
// It is not. Reading the two call sites in the disassembler:
//
//   sub_81CE70   sub_82F0F0(model, a1 + 132, ...)
//   sub_830DC0   sub_82F0F0(this, *(this + 40) + 132, ...)
//
// Both pass +132 of the shared animation context - the same object that holds
// the frame counter at +20 which the once-per-frame guard compares against. It
// is one matrix shared by every model, not that model's placement in the world.
// The other call in sub_830DC0 passes the parent model's +244, which is its
// local 3x3, and no better.
//
// So there is no distance and no screen size at this call site, and level of
// detail driven by anything else is level of detail driven by a guess. The cost
// of guessing wrong is a frozen skeleton on the player's own character or on
// whatever they are fighting, which is not a trade to make blind.
//
// What is missing before that decision can be made is the shape of the work:
// how many models per frame, how many bones between them, and how much of the
// frame it really costs on the machine complaining. A profile says a fifth of
// execution; it does not say whether that is forty models at thirty bones or
// four hundred at three, and those want completely different answers. This
// counts it.
// ============================================================================

#include <windows.h>
#include <cstdint>

#include "anim_census.h"
#include "crash_dumper.h"
#include "config.h"
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);
extern DWORD g_mainThreadId;

MH_STATUS WineSafe_CreateHook(void* target, void* detour, void** original);
MH_STATUS WO_EnableHook(void* target);

namespace AnimCensus {

static constexpr uintptr_t ADDR_AnimateModel = 0x0082F0F0;

// __thiscall with five stack arguments, emulated as __fastcall with the unused
// EDX slot, which is how every other thiscall detour in this project is written.
typedef int(__fastcall* AnimateModel_fn)(void* This, void* edx,
                                         int a2, int a3, float a4, float a5, float a6);
static AnimateModel_fn orig_AnimateModel = nullptr;

static bool g_active = false;

// Per-frame, reset by OnFrame.
static volatile LONG g_callsThisFrame = 0;
static volatile LONG g_bonesThisFrame = 0;

// Running totals and peaks across the session.
static double   g_sumCalls = 0.0;
static double   g_sumBones = 0.0;
static uint64_t g_frames   = 0;
static LONG     g_peakCalls = 0;
static LONG     g_peakBones = 0;

// Timing is sampled rather than measured on every call. This function runs
// hundreds of times a frame, and a QueryPerformanceCounter pair around each one
// would be a fair fraction of what it is trying to weigh - the same mistake as
// putting a cross-module counter inside a three-nanosecond hook.
static constexpr LONG TIME_EVERY_N = 64;
static volatile LONG g_sinceTimed = 0;
static double   g_sampledNs = 0.0;
static uint64_t g_sampledCount = 0;
static double   g_qpcToNs = 0.0;

// Bone count for a model, read the same way sub_82F0F0 reads it:
//   modelData = *(*(this + 44) + 336);  boneCount = modelData[11];
// Guarded because this runs on every animated model and a malformed one must
// cost a skipped count, not a crash.
static uint32_t BoneCountOf(void* This) {
    __try {
        uintptr_t base = *(uintptr_t*)((char*)This + 44);
        if (base < 0x10000 || base >= 0xFFE00000) return 0;
        uintptr_t md = *(uintptr_t*)(base + 336);
        if (md < 0x10000 || md >= 0xFFE00000) return 0;
        uint32_t bones = *(uint32_t*)(md + 11 * 4);
        return bones > 4096 ? 0 : bones;   // clearly wrong, do not count it
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int __fastcall Hooked_AnimateModel(void* This, void* edx,
                                          int a2, int a3, float a4, float a5, float a6) {
    if (!g_active || GetCurrentThreadId() != g_mainThreadId)
        return orig_AnimateModel(This, edx, a2, a3, a4, a5, a6);

    InterlockedIncrement(&g_callsThisFrame);
    uint32_t bones = BoneCountOf(This);
    if (bones) InterlockedExchangeAdd(&g_bonesThisFrame, (LONG)bones);

    // Sampled timing. Note this measures the call including everything it
    // recurses into, which is what a level-of-detail decision would actually
    // save, so it is the number worth having.
    if (InterlockedIncrement(&g_sinceTimed) >= TIME_EVERY_N) {
        InterlockedExchange(&g_sinceTimed, 0);
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        int r = orig_AnimateModel(This, edx, a2, a3, a4, a5, a6);
        QueryPerformanceCounter(&b);
        g_sampledNs += (double)(b.QuadPart - a.QuadPart) * g_qpcToNs;
        g_sampledCount++;
        return r;
    }

    return orig_AnimateModel(This, edx, a2, a3, a4, a5, a6);
}

void OnFrame() {
    if (!g_active) return;

    LONG calls = InterlockedExchange(&g_callsThisFrame, 0);
    LONG bones = InterlockedExchange(&g_bonesThisFrame, 0);
    if (calls == 0) return;   // no world, or nothing animated

    g_frames++;
    g_sumCalls += (double)calls;
    g_sumBones += (double)bones;
    if (calls > g_peakCalls) g_peakCalls = calls;
    if (bones > g_peakBones) g_peakBones = bones;
}

bool Init() {
    if (!Config::g_settings.OptAnimCensus) return true;

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart == 0) return false;
    g_qpcToNs = 1e9 / (double)freq.QuadPart;

    if (WineSafe_CreateHook((void*)ADDR_AnimateModel, (void*)Hooked_AnimateModel,
                            (void**)&orig_AnimateModel) != MH_OK) {
        Log("[AnimCensus] Could not hook the model animation update at 0x%08X",
            (unsigned)ADDR_AnimateModel);
        return false;
    }
    if (WO_EnableHook((void*)ADDR_AnimateModel) != MH_OK) {
        Log("[AnimCensus] Could not enable the hook");
        return false;
    }

    g_active = true;
    CrashDumper::RegisterFeature("AnimCensus");
    Log("[AnimCensus] Counting M2 animation work at 0x%08X (diagnostic - "
        "timing sampled one call in %d)", (unsigned)ADDR_AnimateModel, TIME_EVERY_N);
    return true;
}

// Printed from the periodic report. Shutdown does not run - the DLL exits via
// TerminateProcess - so anything reported only from there is never seen.
void LogStats() {
    if (!g_active || g_frames == 0) return;

    double avgCalls = g_sumCalls / (double)g_frames;
    double avgBones = g_sumBones / (double)g_frames;
    double avgNs    = (g_sampledCount > 0) ? (g_sampledNs / (double)g_sampledCount) : 0.0;

    Log("[AnimCensus] %.1f models/frame (peak %ld), %.0f bones/frame (peak %ld), "
        "%.0f bones per model",
        avgCalls, g_peakCalls, avgBones, g_peakBones,
        avgCalls > 0.0 ? avgBones / avgCalls : 0.0);

    if (g_sampledCount > 0) {
        Log("[AnimCensus] %.2f us per model measured over %llu sampled calls, so "
            "about %.2f ms/frame at the average model count",
            avgNs / 1000.0, (unsigned long long)g_sampledCount,
            avgNs * avgCalls / 1e6);
    }
}

void Shutdown() {
    if (!g_active) return;
    g_active = false;
    MH_DisableHook((void*)ADDR_AnimateModel);
    LogStats();
}

} // namespace AnimCensus
