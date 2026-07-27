#pragma once

// ============================================================================
// Module: hot_patch.h
// ============================================================================

#ifndef HOT_PATCH_H
#define HOT_PATCH_H

namespace HotPatch {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

#endif
