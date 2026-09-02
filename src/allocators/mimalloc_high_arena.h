#pragma once

// Includes nothing on purpose: version.h defines the MinHook helpers only after
// MinHook.h has been seen, and this header is included near the top of
// dllmain.cpp.

namespace MimallocHighArena {

bool Init();
void LogStats();

}  // namespace MimallocHighArena
