#include "mouse_clip_release.h"

namespace MouseClipRelease {

static bool g_hasFocus = true;

bool Init() {
    return true;
}

void Shutdown() {
    ClipCursor(nullptr);
}

void OnFocusChange(bool hasFocus) {
    g_hasFocus = hasFocus;
    if (!g_hasFocus) {
        // Release screen boundary clip locks on mouse cursor coordinates
        ClipCursor(nullptr);
    }
}

void OnFrame() {
    // Focus = the foreground window belongs to our process. Poll it each frame
    // (cheap) and fire OnFocusChange only on transitions.
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    bool hasFocus = (pid == GetCurrentProcessId());
    if (hasFocus != g_hasFocus) {
        OnFocusChange(hasFocus);
    }
}

} // namespace MouseClipRelease
