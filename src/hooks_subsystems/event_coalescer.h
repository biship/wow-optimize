#pragma once

// ============================================================================
// Module: event_coalescer.h
// ============================================================================

extern "C" void EventCoalescer_Flush();

namespace EventCoalescer {
    bool Init();
    void Shutdown();

    // True once Init() has armed the frame-scoped dedup queue. The detour itself
    // lives in LoadingState, which only routes events here while this is true.
    //
    // Exposed as the flag rather than a getter because the check sits on the
    // event-signal path, which the profile shows is one of the hottest pages of
    // this DLL. A cross-module call to read a bool that TEST_DISABLE_EVENT_COALESCER
    // pins to false is pure overhead on every event the client signals.
    extern bool g_active;
    inline bool IsActive() { return g_active; }

    // Returns true if the event was queued/dropped and must NOT reach the client.
    // Called from the LoadingState detour with the raw vararg block.
    bool TryQueue(int eventId, const char* format, void* vaStart);
}
