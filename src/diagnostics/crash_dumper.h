#pragma once

// ============================================================================
// Module: crash_dumper.h
// ============================================================================









#ifndef CRASH_DUMPER_H
#define CRASH_DUMPER_H

// Feature tracking for crash diagnosis
// Each optimization registers itself so crash dumps show exactly what was active
#define MAX_TRACKED_FEATURES 128

struct FeatureState {
    const char* name;        // Feature name (e.g., "AdaptiveGC", "GetStrInline")
    bool        active;      // Currently enabled
    long long   callCount;   // Total invocations since init
    long long   errorCount;  // SEH exceptions caught
    DWORD       lastCallTick;// GetTickCount of last invocation
    const char* lastError;   // Last error description (static string)
};

namespace CrashDumper {
    bool Init();
    void Shutdown();

    // Register a tracked feature for crash diagnostics
    void RegisterFeature(const char* name);

    // Update feature state (call from hooks/fast-paths)
    void FeatureCall(const char* name);
    void FeatureError(const char* name, const char* desc);
    void FeatureSetActive(const char* name, bool active);

    // Get current feature states for crash dump
    int GetFeatureStates(FeatureState* out, int maxCount);

    // Record last hook call for crash context (ring buffer, lock-free)
    void RecordHookCall(const char* hookName, uintptr_t addr);

    // Hot-path variant for very high-frequency hooks (UI accessors etc.).
    // Samples ~1/64 calls so the per-call InterlockedIncrement stays off the
    // frame critical path, and so one hot hook can't flood the 256-slot ring
    // and evict the rarer, riskier hooks we actually want in a crash trace.
    void RecordHookCallHot(const char* hookName, uintptr_t addr);

    // ------------------------------------------------------------------
    // Event trace ("flight recorder")
    //
    // RecordHookCall answers "which hook ran last", but only the handful of
    // opt-in features that call it - so in practice a crash report showed an
    // empty trace. This ring records STATE TRANSITIONS instead: loading screen
    // boundaries, lua_State swaps, D3D9 device resets, cache invalidations,
    // watchdogs firing. Those are what actually explain a crash here, they are
    // rare enough that formatting one costs nothing, and they are recorded
    // unconditionally so the trail exists no matter which features are enabled.
    //
    // Safe from any thread; never allocates. Keep messages short and factual.
    void Trace(const char* fmt, ...);

    // Writes the most recent `count` events to the log, newest first.
    //
    // maxAgeMs bounds how far back an event may be and still be printed. A caller
    // explaining something that just happened - a slow frame - must pass the
    // window it actually covers, or the newest three entries in the ring get
    // presented as the cause when they are minutes old and unrelated. 0 means no
    // bound, which is what the crash handler wants: there, the whole trail is the
    // point. Returns the number of events printed.
    int DumpTrace(int count, DWORD maxAgeMs = 0);
}

// Times a scope and traces it ONLY if it ran longer than thresholdMs.
//
// The event ring is fed by state transitions, which are rare - a session can go
// seventeen minutes recording three of them. That is fine for a crash trail and
// useless for explaining a 100 ms frame, because the newest entries are minutes
// old. This fills the gap without flooding: a probe that stays under its
// threshold writes nothing, so every line it does produce is a stall long enough
// to matter, on work known to run on the main thread.
class StallProbe {
public:
    StallProbe(const char* what, double thresholdMs);
    ~StallProbe();
private:
    const char*   m_what;
    double        m_thresholdMs;
    LARGE_INTEGER m_start;
};

// Same measurement without RAII. MSVC rejects objects requiring unwinding in any
// function that uses __try, and several of the places worth probing are SEH
// guarded, so those call StallProbeBegin/End directly.
LARGE_INTEGER StallProbeBegin();
void StallProbeEnd(const char* what, const LARGE_INTEGER& start, double thresholdMs);

// C-callable wrapper for modules that only see the C interface.
extern "C" void CrashDumper_Trace(const char* fmt, ...);

#endif
