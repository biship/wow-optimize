#include "vertex_buffer_prealloc.h"
#include "version.h"
#include <vector>

extern "C" void Log(const char* fmt, ...);

namespace VertexBufferPrealloc {

static constexpr size_t POOL_SIZE = 128;
static constexpr size_t CHUNK_SIZE = 65536; // 64KB chunks for vertex buffers

static uint8_t* g_poolBuffer = nullptr;
static void* g_freeChunks[POOL_SIZE];
static size_t g_freeCount = 0;
static SRWLOCK g_poolLock = SRWLOCK_INIT;
static uint64_t g_allocations = 0;
static uint64_t g_poolHits = 0;

struct SRWLockGuard {
    SRWLOCK* lock;
    SRWLockGuard(SRWLOCK* l) : lock(l) { AcquireSRWLockExclusive(lock); }
    ~SRWLockGuard() { ReleaseSRWLockExclusive(lock); }
};

bool Init() {
    SRWLockGuard lock(&g_poolLock);
    g_poolBuffer = (uint8_t*)VirtualAlloc(nullptr, POOL_SIZE * CHUNK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_poolBuffer) {
        g_freeCount = POOL_SIZE;
        for (size_t i = 0; i < POOL_SIZE; i++) {
            g_freeChunks[i] = g_poolBuffer + i * CHUNK_SIZE;
        }
        Log("[VertexBufferPrealloc] Pre-allocated %d chunks of %d bytes", POOL_SIZE, CHUNK_SIZE);
    }
    Log("[VertexBufferPrealloc] Active - Model Vertex Buffers Pre-Allocator initialized");
    return true;
}

// Printed from the periodic report. Shutdown does not run - the DLL exits via
// TerminateProcess - so anything reported only from there is never seen.
void LogStats() {
    Log("[VertexBufferPrealloc] Stats: Serviced %lld allocations, %lld pool hits", g_allocations, g_poolHits);
}

void Shutdown() {
    SRWLockGuard lock(&g_poolLock);
    if (g_poolBuffer) {
        VirtualFree(g_poolBuffer, 0, MEM_RELEASE);
        g_poolBuffer = nullptr;
    }
    LogStats();
}

void* AllocateBuffer(size_t size) {
    SRWLockGuard lock(&g_poolLock);
    g_allocations++;
    if (size <= CHUNK_SIZE && g_freeCount > 0 && g_freeCount <= POOL_SIZE) {
        g_poolHits++;
        return g_freeChunks[--g_freeCount];
    }
    // Fallback to 16-byte aligned malloc
    return _aligned_malloc(size, 16);
}

void FreeBuffer(void* ptr) {
    if (!ptr) return;
    SRWLockGuard lock(&g_poolLock);
    if (g_poolBuffer && ptr >= g_poolBuffer && ptr < g_poolBuffer + POOL_SIZE * CHUNK_SIZE) {
        // Double-free and bounds-overflow guard
        if (g_freeCount >= POOL_SIZE) {
            Log("[VertexBufferPrealloc] WARNING: Pool overflow detected in FreeBuffer (g_freeCount=%d). Bypassing pool push.", g_freeCount);
            return;
        }
        for (size_t i = 0; i < g_freeCount; i++) {
            if (g_freeChunks[i] == ptr) {
                Log("[VertexBufferPrealloc] WARNING: Double free detected for pointer %p", ptr);
                return;
            }
        }
        g_freeChunks[g_freeCount++] = ptr;
        return;
    }
    _aligned_free(ptr);
}

} // namespace VertexBufferPrealloc
