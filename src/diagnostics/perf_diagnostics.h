#pragma once
#include <windows.h>

namespace PerfDiagnostics {
    bool Init();
    void Shutdown();
    void OnFrame(double elapsedMs);
    // Full memory/VA/feature snapshot. Also called by the freeze watchdog from a
    // background thread when a "loading" stall runs long enough to be a hang.
    void LogPerformanceSnapshot(double elapsedMs);
}
