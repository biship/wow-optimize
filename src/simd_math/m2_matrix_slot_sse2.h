#pragma once

// Replaces the two sixteen-float copy blocks in sub_82F0F0 (M2_AnimateModel)
// with four SSE2 moves each. See the .cpp for the register contracts.

namespace M2MatrixSlot {

bool Install();
void Shutdown();
void LogStats();

}  // namespace M2MatrixSlot
