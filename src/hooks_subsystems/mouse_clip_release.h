#pragma once
#include <windows.h>

namespace MouseClipRelease {
    bool Init();
    void Shutdown();
    void OnFocusChange(bool hasFocus);
    // Per-frame focus poll: releases any cursor clip region when WoW loses the
    // foreground, so the mouse is never trapped after alt-tab. Only ever releases
    // the clip (ClipCursor(nullptr)); never applies one, so it can't cause the
    // window-slide / camera-spin bugs that clipping-to-client-rect did.
    void OnFrame();
}
