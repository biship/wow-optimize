#pragma once

// ============================================================================
// Module: hooks_logic.h
// Description: Installs and manages target intercepts for subsystem `hooks_logic.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================










bool InstallLogicHooks(void);
void ShutdownLogicHooks(void);
void OnFrameLogicHooks(DWORD mainThreadId);
