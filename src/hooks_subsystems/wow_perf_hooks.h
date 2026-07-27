#pragma once

// ============================================================================
// Module: wow_perf_hooks.h
// Description: Installs and manages target intercepts for subsystem `wow_perf_hooks.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================









#ifndef WOW_PERF_HOOKS_H
#define WOW_PERF_HOOKS_H

namespace WowPerfHooks {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

#endif