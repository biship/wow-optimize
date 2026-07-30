#pragma once

#ifndef D3D9_STATE_CACHE_H
#define D3D9_STATE_CACHE_H

struct IDirect3DDevice9;

namespace D3D9StateCache {

// Initialize the D3D9 state cache, hook SetTexture, SetRenderState, etc.
bool Init();

// Shut down the hooks and release resources
void Shutdown();
void LogStats();

// Draw calls per frame, if the census was switched on.
void ReportDrawCensus();

// Handle device creation (resolve pointers and install hooks)
void OnCreateDevice(struct IDirect3DDevice9* device);

// Clear all cache entries, queries, and vertex buffers
void InvalidateAllCaches(bool safeToRelease);

} // namespace D3D9StateCache

#endif // D3D9_STATE_CACHE_H
