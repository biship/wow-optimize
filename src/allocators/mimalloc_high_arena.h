#pragma once

// Includes nothing on purpose: version.h defines the MinHook helpers only after
// MinHook.h has been seen, and this header is included near the top of
// dllmain.cpp.

namespace MimallocHighArena {

bool Init();
// Hands over another block before the allocator runs out of what it has. Call
// from a background thread; it reserves address space.
void Grow();
void LogStats();

}  // namespace MimallocHighArena
