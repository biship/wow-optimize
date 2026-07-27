#pragma once

// ============================================================================
// Module: wow_source_opt.h
// Description: Installs and manages target intercepts for subsystem `wow_source_opt.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================









#ifndef WOW_SOURCE_OPT_H
#define WOW_SOURCE_OPT_H

namespace WowSourceOpt {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

#endif