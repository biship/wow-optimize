#pragma once

// ============================================================================
// Module: wow_opt_hooks.h
// Description: Installs and manages target intercepts for subsystem `wow_opt_hooks.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================









#ifndef WOW_OPT_HOOKS_H
#define WOW_OPT_HOOKS_H

namespace WowOptHooks {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

#endif