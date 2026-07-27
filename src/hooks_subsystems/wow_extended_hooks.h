#pragma once

// ============================================================================
// Module: wow_extended_hooks.h
// Description: Installs and manages target intercepts for subsystem `wow_extended_hooks.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================









#ifndef WOW_EXTENDED_HOOKS_H
#define WOW_EXTENDED_HOOKS_H

namespace WowExtendedHooks {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

#endif
