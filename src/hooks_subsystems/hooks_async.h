#pragma once

// ============================================================================
// Module: hooks_async.h
// Description: Installs and manages target intercepts for subsystem `hooks_async.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================










bool InstallAsyncHooks(void);
void ShutdownAsyncHooks(void);
void OnFrameAsyncHooks(DWORD mainThreadId);
