#pragma once

// ============================================================================
// Module: obj_vis_cache.h
// ============================================================================









#include <cstdint>
#include <windows.h>

namespace ObjVisCache {

bool Init();
void Shutdown();
void OnFrame();  // Call once per frame on main thread to invalidate stale entries

} // namespace ObjVisCache
