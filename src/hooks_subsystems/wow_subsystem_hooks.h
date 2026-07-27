#pragma once

// ============================================================================
// Module: wow_subsystem_hooks.h
// Description: Installs and manages target intercepts for subsystem `wow_subsystem_hooks.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================









#ifndef WOW_SUBSYSTEM_HOOKS_H
#define WOW_SUBSYSTEM_HOOKS_H

namespace WowSubsystemHooks {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

#endif
