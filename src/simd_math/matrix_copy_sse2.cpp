// ============================================================================
// Module: matrix_copy_sse2.cpp
// ============================================================================

#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <emmintrin.h>
#include <intrin.h>
#include "version.h"
#include "matrix_copy_sse2.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Statistics — plain increments, not Interlocked.
// Both hooked functions run on the main WoW thread only;
// atomic overhead would dwarf the work itself.
// ================================================================
static volatile long g_matcopy_calls = 0;
static volatile long g_matident_calls = 0;

// ================================================================
// Original function pointers
// __fastcall typedef mirrors the __thiscall ABI on x86 MSVC:
// ECX = this, EDX = unused padding.
// ================================================================
typedef float* (__fastcall* MatCopy_t)(float* self, void* edx, float* src);
typedef float* (__fastcall* MatIdentity_t)(float* self, void* edx);

// sub_4C1F00: result = A * B, all three are float[16] passed by stack (__cdecl).
typedef float* (__cdecl* MatMul_t)(float* result, float* a, float* b);

static MatCopy_t     pOrigMatCopy     = nullptr;
static MatIdentity_t pOrigMatIdentity = nullptr;
static MatMul_t      pOrigMatMul      = nullptr;
static volatile long g_matmul_calls   = 0;

typedef float* (__cdecl* MatVec3Mul_t)(float* result, const float* vec3, const float* matrix44);
typedef float* (__cdecl* MatVec4Mul_t)(float* result, const float* vec4, const float* matrix44);

static MatVec3Mul_t  pOrigMatVec3Mul  = nullptr;
static MatVec4Mul_t  pOrigMatVec4Mul  = nullptr;
static volatile long g_matvec3_calls  = 0;
static volatile long g_matvec4_calls  = 0;

// ================================================================
// Precomputed identity matrix rows for the SSE2 store path
// ================================================================
static const __m128 kIdentityRow0 = { 1.0f, 0.0f, 0.0f, 0.0f };
static const __m128 kIdentityRow1 = { 0.0f, 1.0f, 0.0f, 0.0f };
static const __m128 kIdentityRow2 = { 0.0f, 0.0f, 1.0f, 0.0f };
static const __m128 kIdentityRow3 = { 0.0f, 0.0f, 0.0f, 1.0f };

// ================================================================
// sub_407F80: 4x4 matrix copy (247 xrefs)
// Original does 16 scalar FPU load/store pairs.
// 4x SSE2 unaligned 128-bit moves cover all 64 bytes.
// ================================================================
static float* __fastcall HookMatrixCopy(float* self, void* /*edx*/, float* src) {
    ++g_matcopy_calls;

    uintptr_t s = (uintptr_t)self;
    uintptr_t p = (uintptr_t)src;
    if (s > 0x10000 && s < 0xFFE00000 &&
        p > 0x10000 && p < 0xFFE00000) {
        __try {
            _mm_storeu_ps(self,      _mm_loadu_ps(src));
            _mm_storeu_ps(self + 4,  _mm_loadu_ps(src + 4));
            _mm_storeu_ps(self + 8,  _mm_loadu_ps(src + 8));
            _mm_storeu_ps(self + 12, _mm_loadu_ps(src + 12));
            return self;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Bad pointer during load/store (e.g. unmapped page)
        }
    }

    return pOrigMatCopy(self, nullptr, src);
}

// ================================================================
// sub_407F40: 4x4 matrix identity (53 xrefs)
// Original writes 16 immediate floats through the FPU.
// 4x SSE2 stores from compile-time constants.
// ================================================================
static float* __fastcall HookMatrixIdentity(float* self, void* /*edx*/) {
    ++g_matident_calls;

    uintptr_t s = (uintptr_t)self;
    if (s > 0x10000 && s < 0xFFE00000) {
        __try {
            _mm_storeu_ps(self,      kIdentityRow0);
            _mm_storeu_ps(self + 4,  kIdentityRow1);
            _mm_storeu_ps(self + 8,  kIdentityRow2);
            _mm_storeu_ps(self + 12, kIdentityRow3);
            return self;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Bad pointer during store
        }
    }

    return pOrigMatIdentity(self, nullptr);
}

// ================================================================
// sub_4C1F00: 4x4 matrix multiply  result = A * B  (53+ xrefs)
// ================================================================
// Verified convention: result[r*4+c] = sum_k A[r*4+k] * B[k*4+c]
// (row-major C = A*B). The SSE2 form below broadcasts each element of an
// A row across a full B row and accumulates, producing the identical
// products; only the summation order differs, a sub-ULP delta that is
// invisible for transform matrices. It loads all of B and a full A row
// before storing, so it is also safe when result aliases A or B (the
// scalar original is not, but no caller passes aliasing pointers).
static float* __cdecl HookMatrixMultiply(float* result, float* a, float* b) {
    ++g_matmul_calls;

    uintptr_t r = (uintptr_t)result, pa = (uintptr_t)a, pb = (uintptr_t)b;
    if (r > 0x10000 && r < 0xFFE00000 &&
        pa > 0x10000 && pa < 0xFFE00000 &&
        pb > 0x10000 && pb < 0xFFE00000) {
        __try {
            __m128 b0 = _mm_loadu_ps(b);
            __m128 b1 = _mm_loadu_ps(b + 4);
            __m128 b2 = _mm_loadu_ps(b + 8);
            __m128 b3 = _mm_loadu_ps(b + 12);
            
            float out_val[16];
            for (int row = 0; row < 4; ++row) {
                __m128 ar = _mm_loadu_ps(a + row * 4);
                __m128 acc = _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(0,0,0,0)), b0);
                acc = _mm_add_ps(acc, _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(1,1,1,1)), b1));
                acc = _mm_add_ps(acc, _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(2,2,2,2)), b2));
                acc = _mm_add_ps(acc, _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(3,3,3,3)), b3));
                _mm_storeu_ps(out_val + row * 4, acc);
            }
            _ReadWriteBarrier();
            memcpy(result, out_val, 16 * sizeof(float));
            return result;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Unmapped page mid-op — fall through to the original.
        }
    }
    return pOrigMatMul(result, a, b);
}

// ================================================================
// sub_4C21B0: 3D point * 4x4 matrix (100+ xrefs)
// Vectorized via column linear combination using SSE2
// ================================================================
static float* __cdecl Hooked_MatVec3Mul(float* result, const float* vec3, const float* matrix44) {
    ++g_matvec3_calls;

    uintptr_t r = (uintptr_t)result, pv = (uintptr_t)vec3, pm = (uintptr_t)matrix44;
    if (r > 0x10000 && r < 0xFFE00000 &&
        pv > 0x10000 && pv < 0xFFE00000 &&
        pm > 0x10000 && pm < 0xFFE00000) {
        __try {
            double vx = vec3[0];
            double vy = vec3[1];
            double vz = vec3[2];

            double m0 = matrix44[0];
            double m4 = matrix44[4];
            double m8 = matrix44[8];
            double m12 = matrix44[12];

            double m1 = matrix44[1];
            double m5 = matrix44[5];
            double m9 = matrix44[9];
            double m13 = matrix44[13];

            double m2 = matrix44[2];
            double m6 = matrix44[6];
            double m10 = matrix44[10];
            double m14 = matrix44[14];

            double rx = vx * m0 + vy * m4 + vz * m8 + m12;
            double ry = vx * m1 + vy * m5 + vz * m9 + m13;
            double rz = vx * m2 + vy * m6 + vz * m10 + m14;

            result[0] = (float)rx;
            result[1] = (float)ry;
            result[2] = (float)rz;

            return result;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Unmapped page - fallback
        }
    }
    return pOrigMatVec3Mul(result, vec3, matrix44);
}

// ================================================================
// sub_4C2270: 4D vector * 4x4 matrix (20 xrefs)
// Vectorized via column linear combination using SSE2
// ================================================================
static float* __cdecl Hooked_MatVec4Mul(float* result, const float* vec4, const float* matrix44) {
    ++g_matvec4_calls;

    uintptr_t r = (uintptr_t)result, pv = (uintptr_t)vec4, pm = (uintptr_t)matrix44;
    if (r > 0x10000 && r < 0xFFE00000 &&
        pv > 0x10000 && pv < 0xFFE00000 &&
        pm > 0x10000 && pm < 0xFFE00000) {
        __try {
            double vx = vec4[0];
            double vy = vec4[1];
            double vz = vec4[2];
            double vw = vec4[3];

            double m0 = matrix44[0];
            double m4 = matrix44[4];
            double m8 = matrix44[8];
            double m12 = matrix44[12];

            double m1 = matrix44[1];
            double m5 = matrix44[5];
            double m9 = matrix44[9];
            double m13 = matrix44[13];

            double m2 = matrix44[2];
            double m6 = matrix44[6];
            double m10 = matrix44[10];
            double m14 = matrix44[14];

            double m3 = matrix44[3];
            double m7 = matrix44[7];
            double m11 = matrix44[11];
            double m15 = matrix44[15];

            double rx = vx * m0 + vy * m4 + vz * m8 + vw * m12;
            double ry = vx * m1 + vy * m5 + vz * m9 + vw * m13;
            double rz = vx * m2 + vy * m6 + vz * m10 + vw * m14;
            double rw = vx * m3 + vy * m7 + vz * m11 + vw * m15;

            result[0] = (float)rx;
            result[1] = (float)ry;
            result[2] = (float)rz;
            result[3] = (float)rw;

            return result;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Unmapped page - fallback
        }
    }
    return pOrigMatVec4Mul(result, vec4, matrix44);
}

// ================================================================
// sub_4C3420 / sub_4C3600: C3Vector::Normalize (in-place, __thiscall(this))
// ================================================================
// Both do v *= 1.0/sqrt(x*x+y*y+z*z) with x87 fsqrt+fdiv. We replace that with
// full-precision SSE (sqrtss + divss) -- deliberately NOT _mm_rsqrt_ps, whose
// approximation + the missing degenerate guard is exactly what NaN-poisoned the
// quaternion-normalize hook. sqrtss/divss are IEEE round-to-nearest, so the result
// matches the scalar original to sub-ULP (only the x^2+y^2+z^2 accumulation order
// differs, x87's 80-bit vs SSE 32-bit -- invisible for a unit vector).
//
//   sub_4C3420: no guard. On a zero vector the original yields 1.0/0 = +Inf then
//               v*Inf = NaN; SSE divss-by-zero (exceptions masked, as WoW runs)
//               produces the identical Inf/NaN, so behaviour is faithful.
//   sub_4C3600: guarded -- only normalizes when mag^2 > 2^-22, else leaves the
//               vector unchanged. Replicated exactly.
#if !TEST_DISABLE_VEC_NORMALIZE_SSE2
typedef void (__fastcall* Vec3Norm_t)(float* self, void* edx);
static Vec3Norm_t pOrigVec3Norm     = nullptr;  // sub_4C3420 (unguarded)
static Vec3Norm_t pOrigVec3NormSafe = nullptr;  // sub_4C3600 (mag^2 > 2^-22 guard)
static volatile long g_vec3norm_calls = 0;

// 2^-22 == 0x34000000f, the engine's near-zero magnitude cutoff in sub_4C3600.
static const float kVec3NormEps = 0.00000023841858f;

static inline void SSE2_Vec3NormalizeInPlace(float* v, bool guard) {
    float vx_val = v[0];
    float vy_val = v[1];
    float vz_val = v[2];

    // Read exactly 3 floats (never v[3], which may sit on an unmapped next page).
    __m128 xyz = _mm_setr_ps(vx_val, vy_val, vz_val, 0.0f);
    __m128 sq  = _mm_mul_ps(xyz, xyz);                                  // x^2,y^2,z^2,0
    __m128 mag2 = _mm_add_ss(_mm_add_ss(sq,
                      _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(1, 1, 1, 1))), // +y^2
                      _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2, 2, 2, 2))); // +z^2
    if (guard) {
        float m = mag2.m128_f32[0];
        if (!(m > kVec3NormEps)) return;  // leave unchanged, exactly like the engine
    }
    __m128 inv  = _mm_div_ss(_mm_set_ss(1.0f), _mm_sqrt_ss(mag2));      // 1.0/sqrt(mag2)
    __m128 invb = _mm_shuffle_ps(inv, inv, _MM_SHUFFLE(0, 0, 0, 0));    // broadcast
    __m128 res  = _mm_mul_ps(xyz, invb);

    float out_x = res.m128_f32[0];
    float out_y = res.m128_f32[1];
    float out_z = res.m128_f32[2];

    v[0] = out_x;
    v[1] = out_y;
    v[2] = out_z;
}

static void __fastcall Hooked_Vec3Norm(float* self, void* edx) {
    ++g_vec3norm_calls;
    if ((uintptr_t)self > 0x10000 && (uintptr_t)self < 0xFFE00000) {
        __try {
            SSE2_Vec3NormalizeInPlace(self, false);
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Bad pointer surfaced during the read -- nothing was written yet
            // (the fault is on the initial load), so deferring to the original
            // leaves the vector exactly as the engine would handle it.
        }
    }
    pOrigVec3Norm(self, edx);
}

static void __fastcall Hooked_Vec3NormSafe(float* self, void* edx) {
    ++g_vec3norm_calls;
    if ((uintptr_t)self > 0x10000 && (uintptr_t)self < 0xFFE00000) {
        __try {
            SSE2_Vec3NormalizeInPlace(self, true);
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    pOrigVec3NormSafe(self, edx);
}
#endif

// ================================================================
// sub_4C23D0: CMatrix::Transpose  out = transpose(this)  __thiscall(this, out)
// ================================================================
// Pure data movement (16 scalar fld/fstp in the original). _MM_TRANSPOSE4_PS is
// bit-identical -- no arithmetic -- and loads all four rows before storing, so it
// is also safe when out aliases this (the scalar original is not).
//
// sub_4C2300: 3D point * 4x4 matrix, written to BOTH a2 (in place) and a1.
// __cdecl(a1_out, a2_point_inout, a3_matrix). Identical products to MatVec3Mul
// (already shipped against sub_4C21B0); only the accumulation order differs.
#if !TEST_DISABLE_MATRIX_EXT_SSE2
typedef float* (__fastcall* MatTranspose_t)(float* self, void* edx, float* out);
static MatTranspose_t pOrigMatTranspose = nullptr;
static volatile long g_mattranspose_calls = 0;

static float* __fastcall Hooked_MatTranspose(float* self, void* edx, float* out) {
    ++g_mattranspose_calls;
    uintptr_t s = (uintptr_t)self, o = (uintptr_t)out;
    if (s > 0x10000 && s < 0xFFE00000 && o > 0x10000 && o < 0xFFE00000) {
        __try {
            __m128 r0 = _mm_loadu_ps(self);
            __m128 r1 = _mm_loadu_ps(self + 4);
            __m128 r2 = _mm_loadu_ps(self + 8);
            __m128 r3 = _mm_loadu_ps(self + 12);
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
            _mm_storeu_ps(out + 0,  r0);
            _mm_storeu_ps(out + 4,  r1);
            _mm_storeu_ps(out + 8,  r2);
            _mm_storeu_ps(out + 12, r3);
            return out;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return pOrigMatTranspose(self, edx, out);
}

// ================================================================
// sub_4C1BF0: CMatrix::Scale3x3 (upper-left 3x3 *= scalar, __thiscall)
// ================================================================
// Multiplies indices 0,1,2 / 4,5,6 / 8,9,10 by a scalar. Skips the
// translation column (3,7,11) and bottom row (12-15). 37 xrefs in the
// model rendering pipeline (M2 bone/scale updates). The original is 9
// scalar fmuls; SSE2 does 3 vector muls + masked stores.
#if !TEST_DISABLE_MATRIX_EXT_SSE2
typedef void (__fastcall* Scale3x3_t)(float* self, void* edx, float scalar);
static Scale3x3_t pOrigScale3x3 = nullptr;
static volatile long g_scale3x3_calls = 0;

static void __fastcall Hooked_Scale3x3(float* self, void* edx, float scalar) {
    ++g_scale3x3_calls;
    uintptr_t p = (uintptr_t)self;
    if (p > 0x10000 && p < 0xFFE00000) {
        __try {
            __m128 s = _mm_set1_ps(scalar);
            // Row 0: multiply [0..3], store only [0..2]
            __m128 r0 = _mm_mul_ps(_mm_loadu_ps(self), s);
            _mm_store_ss(self,     r0);
            _mm_store_ss(self + 1, _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(1,1,1,1)));
            _mm_store_ss(self + 2, _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(2,2,2,2)));
            // Row 1: multiply [4..7], store only [4..6]
            __m128 r1 = _mm_mul_ps(_mm_loadu_ps(self + 4), s);
            _mm_store_ss(self + 4, r1);
            _mm_store_ss(self + 5, _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1,1,1,1)));
            _mm_store_ss(self + 6, _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(2,2,2,2)));
            // Row 2: multiply [8..11], store only [8..10]
            __m128 r2 = _mm_mul_ps(_mm_loadu_ps(self + 8), s);
            _mm_store_ss(self + 8,  r2);
            _mm_store_ss(self + 9,  _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1,1,1,1)));
            _mm_store_ss(self + 10, _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(2,2,2,2)));
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    pOrigScale3x3(self, edx, scalar);
}
#endif

// ================================================================
// sub_4C3680: CMatrix::From3x3 — expand float[9] → float[16] (5 xrefs)
// ================================================================
// Copies a 3×3 row-major matrix into a 4×4 with identity padding:
//   out[0..2]=in[0..2], out[3]=0
//   out[4..6]=in[3..5], out[7]=0
//   out[8..10]=in[6..8], out[11]=0
//   out[12..14]=in[9..11], out[15]=1
// Used in bone transform construction. SSE2 loads 3 rows of 3 floats
// and stores 4 rows of 4 floats with zero/one padding.
typedef float* (__fastcall* MatFrom3x3_t)(float* self, void* edx, float* src3x3);
static MatFrom3x3_t pOrigMatFrom3x3 = nullptr;
static volatile long g_matfrom3x3_calls = 0;

static float* __fastcall Hooked_MatFrom3x3(float* self, void* edx, float* src) {
    ++g_matfrom3x3_calls;
    uintptr_t s = (uintptr_t)self, p = (uintptr_t)src;
    if (s > 0x10000 && s < 0xFFE00000 && p > 0x10000 && p < 0xFFE00000) {
        __try {
            __m128 r0 = _mm_setr_ps(src[0], src[1], src[2], 0.0f);
            __m128 r1 = _mm_setr_ps(src[3], src[4], src[5], 0.0f);
            __m128 r2 = _mm_setr_ps(src[6], src[7], src[8], 0.0f);
            __m128 r3 = _mm_setr_ps(src[9], src[10], src[11], 1.0f);
            _mm_storeu_ps(self,     r0);
            _mm_storeu_ps(self + 4, r1);
            _mm_storeu_ps(self + 8, r2);
            _mm_storeu_ps(self + 12, r3);
            return self;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return pOrigMatFrom3x3(self, edx, src);
}

typedef float* (__cdecl* PointXformIP_t)(float* a1, float* a2, float* a3);
static PointXformIP_t pOrigPointXformIP = nullptr;
static volatile long g_pointxformip_calls = 0;

static float* __cdecl Hooked_PointXformInPlace(float* a1, float* a2, float* a3) {
    ++g_pointxformip_calls;
    uintptr_t p1 = (uintptr_t)a1, p2 = (uintptr_t)a2, p3 = (uintptr_t)a3;
    if (p1 > 0x10000 && p1 < 0xFFE00000 &&
        p2 > 0x10000 && p2 < 0xFFE00000 &&
        p3 > 0x10000 && p3 < 0xFFE00000) {
        __try {
            double vx = a2[0];
            double vy = a2[1];
            double vz = a2[2];

            double m0 = a3[0];
            double m4 = a3[4];
            double m8 = a3[8];
            double m12 = a3[12];

            double m1 = a3[1];
            double m5 = a3[5];
            double m9 = a3[9];
            double m13 = a3[13];

            double m2 = a3[2];
            double m6 = a3[6];
            double m10 = a3[10];
            double m14 = a3[14];

            double rx = vx * m0 + vy * m4 + vz * m8 + m12;
            double ry = vx * m1 + vy * m5 + vz * m9 + m13;
            double rz = vx * m2 + vy * m6 + vz * m10 + m14;

            a2[0] = (float)rx; a2[1] = (float)ry; a2[2] = (float)rz;
            a1[0] = (float)rx; a1[1] = (float)ry; a1[2] = (float)rz;
            return a1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return pOrigPointXformIP(a1, a2, a3);
}
#endif

// ================================================================
// sub_4C2FC0: rigid-transform inverse builder  __thiscall(this, out)  (~34 xrefs)
// ================================================================
// Builds the inverse of an orthonormal (rotation + translation) 4x4 from the
// input matrix `this` into `out`:
//   out_R   = transpose(upper-left 3x3 of this)
//   out[12+i] = -(R_row_i . t),  t = this[12..14]
//   out[3] = out[7] = out[11] = 0,  out[15] = 1
// The engine first repacks this' 3x3 into a stack scratch via sub_4C51B0 and
// reads from there; since that helper only copies the SAME nine elements
// (this[0,1,2,4,5,6,8,9,10]) we read them directly and skip the call entirely.
// _MM_TRANSPOSE4_PS with a zeroed 4th row yields the transposed rotation rows
// with lane3 already 0; the same transposed rows are exactly the column vectors
// needed for the three translation dot products, so trans = r0*(-tx)+r1*(-ty)+
// r2*(-tz) lands (out12,out13,out14,0). Products and (a+b)+c summation order
// match the FPU original; only x87 80-bit vs SSE 32-bit intermediates differ
// (sub-ULP, invisible for a rigid transform). All reads stay inside the 64-byte
// input matrix; the full 16-float output is written exactly as the original.
#if !TEST_DISABLE_MATRIX_INVERT_SSE2
typedef float* (__fastcall* MatInvRigid_t)(float* self, void* edx, float* out);
static MatInvRigid_t pOrigMatInvRigid = nullptr;
static volatile long g_matinvrigid_calls = 0;

static float* __fastcall Hooked_MatInvertRigid(float* self, void* edx, float* out) {
    ++g_matinvrigid_calls;
    uintptr_t s = (uintptr_t)self, o = (uintptr_t)out;
    if (s > 0x10000 && s < 0xFFE00000 && o > 0x10000 && o < 0xFFE00000) {
        __try {
            __m128 orig0 = _mm_loadu_ps(self);       // M0..M3   (row 0)
            __m128 orig1 = _mm_loadu_ps(self + 4);   // M4..M7   (row 1)
            __m128 orig2 = _mm_loadu_ps(self + 8);   // M8..M11  (row 2)
            float tx = self[12], ty = self[13], tz = self[14];   // translation row

            // Now transpose rotation matrix first
            __m128 r0 = orig0;
            __m128 r1 = orig1;
            __m128 r2 = orig2;
            __m128 r3 = _mm_setzero_ps();         // forces transposed lane3 -> 0
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

            // Compute translation vector using transposed rows:
            // trans = r0*(-tx) + r1*(-ty) + r2*(-tz)
            __m128 trans = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(r0, _mm_set1_ps(-tx)),
                           _mm_mul_ps(r1, _mm_set1_ps(-ty))),
                _mm_mul_ps(r2, _mm_set1_ps(-tz)));          // (out12,out13,out14,0)
            trans = _mm_add_ps(trans, _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f)); // out15=1

            _mm_storeu_ps(out,      r0);
            _mm_storeu_ps(out + 4,  r1);
            _mm_storeu_ps(out + 8,  r2);
            _mm_storeu_ps(out + 12, trans);
            return out;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return pOrigMatInvRigid(self, nullptr, out);
}
#endif

// ================================================================
// sub_4C2120: scalar * 4x4 matrix  __cdecl(out, src, scalar)  (4 xrefs)
// ================================================================
// out[i] = src[i] * scalar for all 16 elements. 16 scalar fmuls -> 4 mul_ps.
// Loads each src row fully before storing, so it is safe if out aliases src.
#if !TEST_DISABLE_MATRIX_MISC_SSE2
typedef float* (__cdecl* MatScalarMul_t)(float* out, float* src, float scalar);
static MatScalarMul_t pOrigMatScalarMul = nullptr;
static volatile long g_matscalarmul_calls = 0;

typedef float* (__cdecl* RowAffinePoint_t)(float* out, float* mat, float* pt);
static RowAffinePoint_t pOrigRowAffinePoint = nullptr;

static float* __cdecl Hooked_MatScalarMul(float* out, float* src, float scalar) {
    ++g_matscalarmul_calls;
    uintptr_t o = (uintptr_t)out, s = (uintptr_t)src;
    if (o > 0x10000 && o < 0xFFE00000 && s > 0x10000 && s < 0xFFE00000) {
        __try {
            __m128 k = _mm_set1_ps(scalar);
            __m128 r0 = _mm_mul_ps(_mm_loadu_ps(src),      k);
            __m128 r1 = _mm_mul_ps(_mm_loadu_ps(src + 4),  k);
            __m128 r2 = _mm_mul_ps(_mm_loadu_ps(src + 8),  k);
            __m128 r3 = _mm_mul_ps(_mm_loadu_ps(src + 12), k);
            _ReadWriteBarrier();
            _mm_storeu_ps(out,      r0);
            _mm_storeu_ps(out + 4,  r1);
            _mm_storeu_ps(out + 8,  r2);
            _mm_storeu_ps(out + 12, r3);
            return out;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return pOrigMatScalarMul(out, src, scalar);
}

// ================================================================
// sub_4C2210: row-major affine 3D point transform  __cdecl(out3, mat16, pt3)  (6 xrefs)
// ================================================================
// out_i = mat[4i]*p.x + mat[4i+1]*p.y + mat[4i+2]*p.z + mat[4i+3], i=0..2.
// (Row-vector form: each output row dotted with the homogeneous point (p,1).)
// Transposing the three matrix rows with a zeroed 4th yields column vectors whose
// linear combination px*c0 + py*c1 + pz*c2 + c3 reproduces exactly those products;
// lane3 stays 0 and is never stored. Reads only mat[0..11] + pt[0..2]; writes 3
// floats. Same four products as the FPU original (summation order sub-ULP).
static float* __cdecl Hooked_RowAffinePoint(float* out, float* mat, float* pt) {
    ++g_matscalarmul_calls;  // shared misc-ops counter
    uintptr_t o = (uintptr_t)out, m = (uintptr_t)mat, p = (uintptr_t)pt;
    if (o > 0x10000 && o < 0xFFE00000 && m > 0x10000 && m < 0xFFE00000 &&
        p > 0x10000 && p < 0xFFE00000) {
        __try {
            __m128 r0 = _mm_loadu_ps(mat);       // M0..M3
            __m128 r1 = _mm_loadu_ps(mat + 4);   // M4..M7
            __m128 r2 = _mm_loadu_ps(mat + 8);   // M8..M11
            __m128 r3 = _mm_setzero_ps();
            float px = pt[0], py = pt[1], pz = pt[2];
            // r0=(M0,M4,M8,0)=col0  r1=(M1,M5,M9,0)=col1  r2=(M2,M6,M10,0)=col2
            //                                              r3=(M3,M7,M11,0)=col3
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(_mm_set1_ps(px), r0),
                           _mm_mul_ps(_mm_set1_ps(py), r1)),
                _mm_add_ps(_mm_mul_ps(_mm_set1_ps(pz), r2), r3));  // (out0,out1,out2,0)
            _mm_store_ss(out,     res);
            _mm_store_ss(out + 1, _mm_shuffle_ps(res, res, _MM_SHUFFLE(1, 1, 1, 1)));
            _mm_store_ss(out + 2, _mm_shuffle_ps(res, res, _MM_SHUFFLE(2, 2, 2, 2)));
            return out;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return pOrigRowAffinePoint(out, mat, pt);
}
#endif

// ================================================================
// sub_4C1B30: in-place local-space translate  __thiscall(this, vec3)  (65+ xrefs)
// ================================================================
// this[12+i] += this[i]*v.x + this[4+i]*v.y + this[8+i]*v.z   (i=0..2)
// i.e. adds R.v to the translation row, where the rotation columns are
// col0=(this[0],this[1],this[2]) = first 3 lanes of row0, etc. The three matrix
// rows loaded as (r0,r1,r2) ARE those columns in lanes 0..2, so
// delta = v.x*r0 + v.y*r1 + v.z*r2 holds the three increments in lanes 0..2
// (lane3 = junk from this[3]/[7]/[11], never used). Only this[12..14] are written
// via scalar adds, leaving this[15] untouched exactly like the original. Same
// products as the FPU original; summation order differs sub-ULP.
#if !TEST_DISABLE_MATRIX_TRANSLATE_SSE2
typedef float* (__fastcall* MatTranslate_t)(float* self, void* edx, float* vec3);
static MatTranslate_t pOrigMatTranslate = nullptr;
static volatile long g_mattranslate_calls = 0;

static float* __fastcall Hooked_MatTranslateLocal(float* self, void* edx, float* vec3) {
    ++g_mattranslate_calls;
    uintptr_t s = (uintptr_t)self, v = (uintptr_t)vec3;
    if (s > 0x10000 && s < 0xFFE00000 && v > 0x10000 && v < 0xFFE00000) {
        __try {
            double vx = vec3[0];
            double vy = vec3[1];
            double vz = vec3[2];

            double r0 = self[0];
            double r4 = self[4];
            double r8 = self[8];

            double r1 = self[1];
            double r5 = self[5];
            double r9 = self[9];

            double r2 = self[2];
            double r6 = self[6];
            double r10 = self[10];

            self[12] = (float)(self[12] + vx * r0 + vy * r4 + vz * r8);
            self[13] = (float)(self[13] + vx * r1 + vy * r5 + vz * r9);
            self[14] = (float)(self[14] + vx * r2 + vy * r6 + vz * r10);
            return vec3;   // original returns the vec3 argument
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return pOrigMatTranslate(self, nullptr, vec3);
}
#endif

// ================================================================
// Install hooks
// ================================================================
bool InstallMatrixCopySSE2() {
#if !TEST_DISABLE_MATRIX_COPY
    struct HookDef {
        void*       addr;
        void*       hook;
        void**      orig;
        const char* name;
        uint32_t    xrefs;
    };

    HookDef hooks[] = {
        { (void*)0x00407F80, (void*)HookMatrixCopy,     (void**)&pOrigMatCopy,     "MatrixCopy",     247 },
        { (void*)0x00407F40, (void*)HookMatrixIdentity, (void**)&pOrigMatIdentity, "MatrixIdentity",  53 },
    };

    int installed = 0;
    for (auto& h : hooks) {
        if (WineSafe_CreateHook(h.addr, h.hook, h.orig) == MH_OK) {
             if (WO_EnableHook(h.addr) == MH_OK) {
                 installed++;
                 Log("[MatrixSSE2] Hooked %s at 0x%08X (%d xrefs)", h.name, (DWORD)(uintptr_t)h.addr, h.xrefs);
             }
        }
    }

    Log("[MatrixSSE2] Installed %d/%d hooks (total %d xrefs)",
        installed, (int)(sizeof(hooks) / sizeof(hooks[0])),
        247 + 53);
#else
    Log("[MatrixSSE2] Matrix copy/identity hooks DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_MULTIPLY
    if (WineSafe_CreateHook((void*)0x004C1F00, (void*)HookMatrixMultiply,
                            (void**)&pOrigMatMul) == MH_OK &&
        WO_EnableHook((void*)0x004C1F00) == MH_OK) {
        Log("[MatrixSSE2] Hooked MatrixMultiply at 0x004C1F00 (SSE2, verified A*B)");
    } else {
        Log("[MatrixSSE2] MatrixMultiply hook FAILED");
    }
#else
    Log("[MatrixSSE2] MatrixMultiply DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_VECTOR_SSE2
    if (WineSafe_CreateHook((void*)0x004C21B0, (void*)Hooked_MatVec3Mul,
                            (void**)&pOrigMatVec3Mul) == MH_OK &&
        WO_EnableHook((void*)0x004C21B0) == MH_OK) {
        Log("[MatrixSSE2] Hooked MatVec3Mul at 0x004C21B0 (SSE2, 100+ xrefs)");
    } else {
        Log("[MatrixSSE2] MatVec3Mul hook FAILED");
    }

    if (WineSafe_CreateHook((void*)0x004C2270, (void*)Hooked_MatVec4Mul,
                            (void**)&pOrigMatVec4Mul) == MH_OK &&
        WO_EnableHook((void*)0x004C2270) == MH_OK) {
        Log("[MatrixSSE2] Hooked MatVec4Mul at 0x004C2270 (SSE2, 20 xrefs)");
    } else {
        Log("[MatrixSSE2] MatVec4Mul hook FAILED");
    }
#else
    Log("[MatrixSSE2] Matrix-Vector hooks DISABLED via feature flag");
#endif

#if !TEST_DISABLE_VEC_NORMALIZE_SSE2
    if (WineSafe_CreateHook((void*)0x004C3420, (void*)Hooked_Vec3Norm,
                            (void**)&pOrigVec3Norm) == MH_OK &&
        WO_EnableHook((void*)0x004C3420) == MH_OK) {
        Log("[MatrixSSE2] Hooked C3Vector::Normalize at 0x004C3420 (SSE2 sqrtss, 12 callers)");
    } else {
        Log("[MatrixSSE2] C3Vector::Normalize hook FAILED");
    }

    if (WineSafe_CreateHook((void*)0x004C3600, (void*)Hooked_Vec3NormSafe,
                            (void**)&pOrigVec3NormSafe) == MH_OK &&
        WO_EnableHook((void*)0x004C3600) == MH_OK) {
        Log("[MatrixSSE2] Hooked C3Vector::Normalize(guarded) at 0x004C3600 (SSE2 sqrtss, 2^-22 guard, 22 callers)");
    } else {
        Log("[MatrixSSE2] C3Vector::Normalize(guarded) hook FAILED");
    }
#else
    Log("[MatrixSSE2] Vector-Normalize hooks DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_EXT_SSE2
    if (WineSafe_CreateHook((void*)0x004C23D0, (void*)Hooked_MatTranspose,
                            (void**)&pOrigMatTranspose) == MH_OK &&
        WO_EnableHook((void*)0x004C23D0) == MH_OK) {
        Log("[MatrixSSE2] Hooked CMatrix::Transpose at 0x004C23D0 (SSE2 _MM_TRANSPOSE4_PS)");
    } else {
        Log("[MatrixSSE2] CMatrix::Transpose hook FAILED");
    }

    /*
    if (WineSafe_CreateHook((void*)0x004C2300, (void*)Hooked_PointXformInPlace,
                            (void**)&pOrigPointXformIP) == MH_OK &&
        WO_EnableHook((void*)0x004C2300) == MH_OK) {
        Log("[MatrixSSE2] Hooked PointTransformInPlace at 0x004C2300 (SSE2, 65 callers)");
    } else {
        Log("[MatrixSSE2] PointTransformInPlace hook FAILED");
    }
    */

    if (WineSafe_CreateHook((void*)0x004C1BF0, (void*)Hooked_Scale3x3,
                            (void**)&pOrigScale3x3) == MH_OK &&
        WO_EnableHook((void*)0x004C1BF0) == MH_OK) {
        Log("[MatrixSSE2] Hooked CMatrix::Scale3x3 at 0x004C1BF0 (SSE2, 37 callers)");
    } else {
        Log("[MatrixSSE2] CMatrix::Scale3x3 hook FAILED");
    }

    if (WineSafe_CreateHook((void*)0x004C3680, (void*)Hooked_MatFrom3x3,
                            (void**)&pOrigMatFrom3x3) == MH_OK &&
        WO_EnableHook((void*)0x004C3680) == MH_OK) {
        Log("[MatrixSSE2] Hooked CMatrix::From3x3 at 0x004C3680 (SSE2, 5 callers)");
    } else {
        Log("[MatrixSSE2] CMatrix::From3x3 hook FAILED");
    }
#else
    Log("[MatrixSSE2] Matrix-Ext hooks DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_INVERT_SSE2
    if (WineSafe_CreateHook((void*)0x004C2FC0, (void*)Hooked_MatInvertRigid,
                            (void**)&pOrigMatInvRigid) == MH_OK &&
        WO_EnableHook((void*)0x004C2FC0) == MH_OK) {
        Log("[MatrixSSE2] Hooked CMatrix::InvertRigid at 0x004C2FC0 (SSE2, ~34 callers)");
    } else {
        Log("[MatrixSSE2] CMatrix::InvertRigid hook FAILED");
    }
#else
    Log("[MatrixSSE2] CMatrix::InvertRigid DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_MISC_SSE2
    if (WineSafe_CreateHook((void*)0x004C2120, (void*)Hooked_MatScalarMul,
                            (void**)&pOrigMatScalarMul) == MH_OK &&
        WO_EnableHook((void*)0x004C2120) == MH_OK) {
        Log("[MatrixSSE2] Hooked CMatrix::ScalarMul at 0x004C2120 (SSE2, 4 callers)");
    } else {
        Log("[MatrixSSE2] CMatrix::ScalarMul hook FAILED");
    }

    if (WineSafe_CreateHook((void*)0x004C2210, (void*)Hooked_RowAffinePoint,
                            (void**)&pOrigRowAffinePoint) == MH_OK &&
        WO_EnableHook((void*)0x004C2210) == MH_OK) {
        Log("[MatrixSSE2] Hooked RowAffinePoint at 0x004C2210 (SSE2, 6 callers)");
    } else {
        Log("[MatrixSSE2] RowAffinePoint hook FAILED");
    }
#else
    Log("[MatrixSSE2] Matrix-Misc hooks DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_TRANSLATE_SSE2
    if (WineSafe_CreateHook((void*)0x004C1B30, (void*)Hooked_MatTranslateLocal,
                            (void**)&pOrigMatTranslate) == MH_OK &&
        WO_EnableHook((void*)0x004C1B30) == MH_OK) {
        Log("[MatrixSSE2] Hooked CMatrix::TranslateLocal at 0x004C1B30 (SSE2, 65+ callers)");
    } else {
        Log("[MatrixSSE2] CMatrix::TranslateLocal hook FAILED");
    }
#else
    Log("[MatrixSSE2] CMatrix::TranslateLocal DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_COPY
    return installed == (int)(sizeof(hooks) / sizeof(hooks[0]));
#else
    return true;
#endif
}

// ================================================================
// Cleanup
// ================================================================
void ShutdownMatrixCopySSE2() {
    MH_DisableHook((void*)0x00407F80);
    MH_DisableHook((void*)0x00407F40);
#if !TEST_DISABLE_MATRIX_MULTIPLY
    MH_DisableHook((void*)0x004C1F00);
#endif
#if !TEST_DISABLE_MATRIX_VECTOR_SSE2
    MH_DisableHook((void*)0x004C21B0);
    MH_DisableHook((void*)0x004C2270);
#endif
#if !TEST_DISABLE_VEC_NORMALIZE_SSE2
    MH_DisableHook((void*)0x004C3420);
    MH_DisableHook((void*)0x004C3600);
    Log("[MatrixSSE2] Stats: Vec3Normalize=%ld", g_vec3norm_calls);
#endif
#if !TEST_DISABLE_MATRIX_EXT_SSE2
    MH_DisableHook((void*)0x004C23D0);
    MH_DisableHook((void*)0x004C2300);
    MH_DisableHook((void*)0x004C1BF0);
    MH_DisableHook((void*)0x004C3680);
    Log("[MatrixSSE2] Stats: Transpose=%ld  PointXformIP=%ld  Scale3x3=%ld  From3x3=%ld",
        g_mattranspose_calls, g_pointxformip_calls, g_scale3x3_calls, g_matfrom3x3_calls);
#endif
#if !TEST_DISABLE_MATRIX_INVERT_SSE2
    MH_DisableHook((void*)0x004C2FC0);
    Log("[MatrixSSE2] Stats: InvertRigid=%ld", g_matinvrigid_calls);
#endif
#if !TEST_DISABLE_MATRIX_MISC_SSE2
    MH_DisableHook((void*)0x004C2120);
    MH_DisableHook((void*)0x004C2210);
    Log("[MatrixSSE2] Stats: MatrixMisc(ScalarMul+RowAffine)=%ld", g_matscalarmul_calls);
#endif
#if !TEST_DISABLE_MATRIX_TRANSLATE_SSE2
    MH_DisableHook((void*)0x004C1B30);
    Log("[MatrixSSE2] Stats: TranslateLocal=%ld", g_mattranslate_calls);
#endif

    Log("[MatrixSSE2] Stats: MatrixCopy=%ld  MatrixIdentity=%ld  MatrixMul=%ld  MatVec3=%ld  MatVec4=%ld",
        g_matcopy_calls, g_matident_calls, g_matmul_calls, g_matvec3_calls, g_matvec4_calls);
}
