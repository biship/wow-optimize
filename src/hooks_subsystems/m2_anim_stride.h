#pragma once

// Holds a distant model's skeleton for a frame instead of re-solving every bone,
// by taking the client's own "this model has no bones" branch out of the loop.
// See the .cpp for why that branch is the only safe place to cut.

namespace M2AnimStride {

bool Install();
void Shutdown();
// The camera, from the maintenance tick. That runs about once in four frames,
// which is often enough for a camera and far too coarse for a stride phase.
void OnFrame();
// The frame counter the stride phase turns on, from the Present hook, which is
// a frame at any frame rate.
void OnPresent();
void LogStats();

}  // namespace M2AnimStride
