// ============================================================================
// Module: loading_state.h
// Description: Native loading-screen / combat state detection from the client's
//              own event stream, independent of the !LuaBoost addon.
// ============================================================================

#pragma once

namespace LoadingState {
    // Installs the FrameScript_SignalEvent detour. Always installed - the loading
    // state it publishes gates safety bypasses across many other subsystems.
    bool Init();
    void Shutdown();

    // True between PLAYER_LEAVING_WORLD and PLAYER_ENTERING_WORLD.
    bool IsLoading();

    // Accounting for where a loading screen's time goes.
    //
    // The project has spent years adding prefetch, memory-mapped archives and
    // async texture decode to make loading faster, and has never measured what a
    // load is actually made of. Prefetching only helps if loading is waiting on
    // the disk; if it is dominated by decompression or scene building, more
    // prefetch is free of both cost and benefit. This measures the split so the
    // next change to the loading path is aimed at something.
    //
    // Only called while IsLoading() is true, so gameplay pays nothing.
    void NoteRead(double ms, unsigned int bytes);

    // Whether the ReadFile hook that feeds NoteRead is actually installed. It is
    // compiled out by CRASH_TEST_DISABLE_READFILE in the shipped build, and the
    // first real log carrying this report said "0 ms (0%) inside ReadFile" for
    // every load - which reads as a measurement of the loading path and is
    // nothing of the kind. Unobserved and zero have to look different.
    void SetReadHookInstalled(bool installed);

    // Session summary: how many loads, how long, and how much of it was I/O.
    void ReportLoadTimes();
}
