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
}
