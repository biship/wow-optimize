// ============================================================================
// Module: horizon_occlusion_sse2.cpp
//
// sub_78F6A0 builds the terrain horizon: it projects a strip of vertices, then
// rasterises them into a 384-column array of floats holding, per screen column,
// the highest thing seen there. Terrain culling reads that array afterwards.
//
// A tester's sampling profile puts it at 2.46% of main-thread execution, the
// third largest single entry after the M2 animation family and the world
// visibility traversal.
//
// The projection loop is already as fast as we can make it - it calls sub_4C21B0
// once per vertex, which this project replaced with packed-double SSE2 last
// week. What is left scalar is the rasterisation: for each segment between two
// projected vertices, a column range is computed and every column in it is
// updated. Those two inner loops walk up to 384 floats one at a time.
//
// The max-update loop vectorises exactly - it is a maximum of one scalar across
// a contiguous span, which is _mm_max_ps four columns at a time. The other
// branch writes a sentinel only where a byte mask is clear, which does not
// vectorise as cleanly and is left scalar; mirroring the client exactly matters
// more there than speed.
//
// Everything about this replacement is checked against the client's own routine
// on real data before it is trusted. The function's entire effect is the 384
// floats it leaves behind, so the check is: snapshot that array, run ours, keep
// the result, restore the snapshot, run the client's, compare. Any difference
// and every later call goes to the original. Replacing a culling function that
// decides what terrain is drawn, without checking it agrees, is how this project
// spent a week undoing an SSE2 matrix multiply that sat a hundred times outside
// its own declared tolerance.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>

#include "horizon_occlusion_sse2.h"
#include "config.h"
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace HorizonOcclusion {

static constexpr uintptr_t ADDR_HorizonBuild = 0x0078F6A0;
static constexpr uintptr_t ADDR_PointXMat4   = 0x004C21B0;

// Client globals the function reads and writes.
static constexpr uintptr_t ADDR_EnableFlags  = 0x00CD774C;   // bit 5 gates the whole thing
static constexpr uintptr_t ADDR_PitchCos     = 0x00CD8F7C;   // must be within +/-0.9
static constexpr uintptr_t ADDR_Scratch      = 0x00CD8FD8;   // projected vertices, 3 floats each
static constexpr uintptr_t ADDR_ViewMatrix   = 0x00ADF460;
static constexpr uintptr_t ADDR_ColumnBias   = 0x00ADF454;
static constexpr uintptr_t ADDR_Horizon      = 0x00CD8938;   // 384 floats, the output
static constexpr uintptr_t ADDR_ColumnFlags  = 0x00CD87B8;   // 384 bytes

static constexpr int   COLUMNS       = 384;
static constexpr int   COLUMN_ORIGIN = 192;
static constexpr float SENTINEL      = -1000001.0f;
static constexpr float MIN_DEPTH     = 0.027777778f;   // 1/36, the client's own constant

typedef void (__cdecl *HorizonBuild_fn)(int a1, int a2, uint32_t* indices, int count,
                                        float* offset, int mode);
typedef float* (__cdecl *PointXMat4_fn)(float* out, const float* vec, const float* mat);

static HorizonBuild_fn orig_HorizonBuild = nullptr;
static PointXMat4_fn   PointXMat4        = (PointXMat4_fn)ADDR_PointXMat4;

static bool g_active = false;

// Verification state. Same shape as the frustum and normalise checks.
static volatile long g_checked   = 0;
static bool          g_trusted   = false;
static bool          g_abandoned = false;
static constexpr long VERIFY_CALLS = 512;   // each check costs a full second run

static volatile long g_calls = 0;

// Project the vertex strip into the client's scratch array, exactly as the
// original does: fetch by index, add the caller's offset, transform, then divide
// x and y by z while leaving z alone.
static void ProjectVertices(int xyBase, int zBase, const uint32_t* indices,
                            int count, const float* offset) {
    float* scratch = (float*)ADDR_Scratch;
    const float* mat = (const float*)ADDR_ViewMatrix;

    for (int i = 0; i < count; ++i) {
        uint32_t idx = indices[i];
        float* p = scratch + i * 3;

        p[0] = *(const float*)(xyBase + 12 * idx)     + offset[0];
        p[1] = *(const float*)(xyBase + 12 * idx + 4) + offset[1];
        p[2] = *(const float*)(zBase  +  4 * idx)     + offset[2];

        float out[4];
        float* r = PointXMat4(out, p, mat);
        p[0] = r[0];
        p[1] = r[1];
        p[2] = r[2];

        float invZ = 1.0f / p[2];
        p[0] *= invZ;
        p[1] *= invZ;
    }
}

// Column range for a segment, with the original's swap-and-clamp order kept.
static inline void ColumnRange(float x0, float x1, int& lo, int& hi) {
    float bias = *(const float*)ADDR_ColumnBias;
    int a = (int)(x0 * 64.0f - bias) + COLUMN_ORIGIN;
    int b = (int)(x1 * 64.0f - bias) + COLUMN_ORIGIN;
    if (b < a) { hi = a; lo = b; } else { lo = a; hi = b; }
    if (lo < 0) lo = 0;
    if (hi >= COLUMNS) hi = COLUMNS - 1;
}

// The vectorised half: raise every column in [lo,hi] to at least `value`.
static inline void RaiseSpan(float* horizon, int lo, int hi, float value) {
    int i = lo;

    // Head, until the span is four-aligned.
    for (; i <= hi && (i & 3) != 0; ++i) {
        if (value > horizon[i]) horizon[i] = value;
    }

    __m128 v = _mm_set1_ps(value);
    for (; i + 3 <= hi; i += 4) {
        __m128 cur = _mm_loadu_ps(horizon + i);
        _mm_storeu_ps(horizon + i, _mm_max_ps(cur, v));
    }

    for (; i <= hi; ++i) {
        if (value > horizon[i]) horizon[i] = value;
    }
}

static void BuildHorizon(int xyBase, int zBase, uint32_t* indices, int count,
                         float* offset, int mode) {
    ProjectVertices(xyBase, zBase, indices, count, offset);

    const float* scratch = (const float*)ADDR_Scratch;
    float* horizon = (float*)ADDR_Horizon;
    const uint8_t* flags = (const uint8_t*)ADDR_ColumnFlags;

    int segments = count - 1;
    if (segments <= 0) return;

    if (mode) {
        // Clear columns the segment covers, but only where the flag bit is not
        // set. Left scalar: the mask makes a vector store conditional, and being
        // byte-for-byte the same as the client matters more here than speed.
        for (int s = 0; s < segments; ++s) {
            int lo, hi;
            ColumnRange(scratch[s * 3], scratch[(s + 1) * 3], lo, hi);
            for (int i = lo; i <= hi; ++i) {
                if ((flags[i] & 1) == 0) horizon[i] = SENTINEL;
            }
        }
        return;
    }

    for (int s = 0; s < segments; ++s) {
        const float* a = scratch + s * 3;
        const float* b = scratch + (s + 1) * 3;

        // Both ends must be far enough in front of the camera.
        if (!(a[2] >= MIN_DEPTH && b[2] >= MIN_DEPTH)) continue;

        float value = (b[1] <= a[1]) ? b[1] : a[1];

        int lo, hi;
        ColumnRange(a[0], b[0], lo, hi);
        if (lo <= hi) RaiseSpan(horizon, lo, hi, value);
    }
}

static void __cdecl Hooked_HorizonBuild(int xyBase, int zBase, uint32_t* indices,
                                        int count, float* offset, int mode) {
    InterlockedIncrement(&g_calls);

    if (g_abandoned) {
        orig_HorizonBuild(xyBase, zBase, indices, count, offset, mode);
        return;
    }

    // The client's own gate. Skipping it would run the body in states where the
    // original does nothing at all.
    uint32_t enableFlags = *(const uint32_t*)ADDR_EnableFlags;
    float pitch = *(const float*)ADDR_PitchCos;
    if ((enableFlags & 0x20) == 0 || pitch < -0.89999998f || pitch > 0.89999998f) {
        return;
    }

    if (count <= 0 || !indices || !offset) {
        orig_HorizonBuild(xyBase, zBase, indices, count, offset, mode);
        return;
    }

    if (g_trusted) {
        __try {
            BuildHorizon(xyBase, zBase, indices, count, offset, mode);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_abandoned = true;
            Log("[Horizon] Faulted while building - handing every call back to the "
                "original");
            orig_HorizonBuild(xyBase, zBase, indices, count, offset, mode);
        }
        return;
    }

    // Verification pass. The whole effect of this function is the 384 floats it
    // leaves behind, so compare those: snapshot, run ours, keep the result,
    // restore, run the client's, compare.
    float* horizon = (float*)ADDR_Horizon;
    float before[COLUMNS];
    float ours[COLUMNS];

    __try {
        memcpy(before, horizon, sizeof(before));
        BuildHorizon(xyBase, zBase, indices, count, offset, mode);
        memcpy(ours, horizon, sizeof(ours));
        memcpy(horizon, before, sizeof(before));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_abandoned = true;
        Log("[Horizon] Faulted during verification - handing every call back to "
            "the original");
        orig_HorizonBuild(xyBase, zBase, indices, count, offset, mode);
        return;
    }

    orig_HorizonBuild(xyBase, zBase, indices, count, offset, mode);

    long n = InterlockedIncrement(&g_checked);
    for (int i = 0; i < COLUMNS; ++i) {
        if (horizon[i] != ours[i]) {
            g_abandoned = true;
            Log("[Horizon] Disagreed with the client at column %d on call %ld "
                "(client %.9g, ours %.9g) - handing every call back to the original",
                i, n, horizon[i], ours[i]);
            return;   // the client's output is already in place
        }
    }

    if (n >= VERIFY_CALLS) {
        g_trusted = true;
        Log("[Horizon] Matched the client's output exactly on %ld consecutive real "
            "calls - running ours alone from here", n);
    }
}

bool Init() {
    if (!Config::g_settings.OptHorizonOcclusionSse2) return true;

    if (WineSafe_CreateHook((void*)ADDR_HorizonBuild, (void*)Hooked_HorizonBuild,
                            (void**)&orig_HorizonBuild) != MH_OK) {
        Log("[Horizon] Could not hook the horizon builder at 0x%08X",
            (unsigned)ADDR_HorizonBuild);
        return false;
    }
    if (WO_EnableHook((void*)ADDR_HorizonBuild) != MH_OK) {
        Log("[Horizon] Could not enable the hook");
        return false;
    }

    g_active = true;
    Log("[Horizon] SSE2 terrain horizon builder installed at 0x%08X - verifying "
        "against the client for the first %ld calls",
        (unsigned)ADDR_HorizonBuild, VERIFY_CALLS);
    return true;
}

void LogStats() {
    if (!g_active) return;
    Log("[Horizon] %ld calls, %s",
        g_calls,
        g_abandoned ? "abandoned - the client's routine is doing the work"
                    : (g_trusted ? "verified, running ours"
                                 : "still verifying against the client"));
}

void Shutdown() {
    if (!g_active) return;
    g_active = false;
    MH_DisableHook((void*)ADDR_HorizonBuild);
    LogStats();
}

} // namespace HorizonOcclusion
