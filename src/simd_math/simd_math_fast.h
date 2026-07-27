#pragma once

// ============================================================================
// Module: simd_math_fast.h
// Description: SSE2 fast paths for the engine's matrix-vector multiply and
//              vector normalize.
// Safety & Threading: Thread-safe, executes on main/render threads.
// ============================================================================

namespace SimdMathFast {

bool Init();
void Shutdown();

} // namespace SimdMathFast
