#pragma once

// ============================================================================
// Module: hooks_memory.h
// Description: Installs and manages target intercepts for subsystem `hooks_memory.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================










bool InstallMemoryHooks(void);
void ShutdownMemoryHooks(void);
void ClearGuidHashTable(void);  // zeroes the entire 16384-entry GUID cache
