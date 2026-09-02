#pragma once
#include <windows.h>

namespace PerfDiagnostics {
    bool Init();
    void Shutdown();

    // How many frames ran past 100ms, and how many were described in full.
    void LogStats();
    void OnFrame(double elapsedMs);
    // Full memory/VA/feature snapshot. Also called by the freeze watchdog from a
    // background thread when a "loading" stall runs long enough to be a hang.
    void LogPerformanceSnapshot(double elapsedMs);
    // Names what occupies the low 2GB - the half a 32-bit client allocates
    // from. Call it from a background thread; it is a full VirtualQuery walk.
    void LogLowHalfOccupancy(const char* why);
}
