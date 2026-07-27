#pragma once

// ============================================================================
// Module: frame_arena.h
// ============================================================================










// 16 MB bump-allocator for sub-1KB wow.exe-originated mallocs. Resets
// on the main-thread Sleep boundary. User pointers carry a magic
// header so foreign frees (mimalloc / CRT) can range-test in O(1).

#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace FrameArena {

bool Init();
void Shutdown();

void* TryAlloc(size_t size);
bool  Owns(const void* p);
size_t SizeOf(const void* p);
void Reset();

// Stat hook for hooked_free / hooked_realloc when they detect arena
// pointers and skip the actual free.
void NoteNoOpFree();

struct Stats {
    bool     active;
    uint32_t capacity;
    uint32_t inUse;
    uint32_t resets;
    uint64_t totalAllocs;
    uint64_t totalAllocBytes;
    uint64_t fallbacks;
    uint64_t freesNoOp;
};
void GetStats(Stats* out);

} // namespace FrameArena
