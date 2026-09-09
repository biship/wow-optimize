#pragma once

// Includes nothing: version.h defines the MinHook helpers only after MinHook.h
// has been seen, and this header is reached from dllmain.cpp near the top.

#include <windows.h>

namespace ClientWriteBatch {

// The client's own write wrapper, sub_454910. The flush goes back through it so
// the handle check, the byte count it writes back and the error it reports are
// the client's, not a second implementation of them.
typedef char (__cdecl* WriteFn)(void* fileObj, const void* buf,
                                void* overlapped, unsigned long* pBytes);

bool Init(WriteFn writer, bool closeHookInstalled);

// True when the write was taken into the buffer and must not be passed on.
bool TryAbsorb(void* fileObj, const void* buf, unsigned long bytes,
               void* overlapped);

// Flush if this handle is the one being buffered. stopVerifying is for a seek,
// after which the file's size no longer has to equal what was written.
void FlushHandle(HANDLE h, bool stopVerifying);

// Flush and check the file's size against everything the client handed over.
// Call before the handle is actually closed, while the size is still readable.
void OnClosing(HANDLE h);

void FlushAll(const char* why);
void LogStats();

}  // namespace ClientWriteBatch
