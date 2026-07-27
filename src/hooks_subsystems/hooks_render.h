#pragma once

// ============================================================================
// Module: hooks_render.h
// Description: Installs and manages target intercepts for subsystem `hooks_render.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================










bool InstallRenderHooks(void);
void ShutdownRenderHooks(void);
void OnFrameRenderHooks(DWORD mainThreadId);
