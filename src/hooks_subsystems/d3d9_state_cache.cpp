// ============================================================================
// Module: d3d9_state_cache.cpp
// Description: Filters redundant D3D9 state modifications to reduce context switches.
// Safety & Threading: Main thread only. Invalidates cache on Reset().
// ============================================================================

#include "d3d9_state_cache.h"
#include "MinHook.h"
#include "version.h"
#include "mip_bias_governor.h"
#include <d3d9.h>
#include "d3d9_render_thread.h"
#include "config.h"
#include "dxvk_bridge.h"
#include "font_glyph_cache.h"
#include "vertex_buffer_prealloc.h"
#include "texture_unload_delay.h"
#include <atomic>

extern "C" void Log(const char* fmt, ...);
extern DWORD g_mainThreadId;
extern volatile LONG g_deviceResetCounter;
extern void RenderStateDedup_ClearCache(void);

namespace D3D9StateCache {

// Original function pointers


typedef HRESULT (WINAPI *SetRenderState_fn)(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value);
SetRenderState_fn orig_SetRenderState = nullptr;

typedef HRESULT (WINAPI *SetTransform_fn)(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix);
SetTransform_fn orig_SetTransform = nullptr;

typedef HRESULT (WINAPI *SetViewport_fn)(IDirect3DDevice9* device, const D3DVIEWPORT9* viewport);
SetViewport_fn orig_SetViewport = nullptr;

typedef HRESULT (WINAPI *CreateVertexBuffer_fn)(IDirect3DDevice9* device, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle);
static CreateVertexBuffer_fn orig_CreateVertexBuffer = nullptr;

typedef HRESULT (WINAPI *VB_Lock_fn)(IDirect3DVertexBuffer9* vb, UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags);
static VB_Lock_fn orig_VB_Lock = nullptr;

typedef HRESULT (WINAPI *VB_Unlock_fn)(IDirect3DVertexBuffer9* vb);
static VB_Unlock_fn orig_VB_Unlock = nullptr;

typedef HRESULT (WINAPI *SetVertexShaderConstantF_fn)(IDirect3DDevice9* device, UINT StartRegister, const float* pConstantData, UINT Vector4fCount);
SetVertexShaderConstantF_fn orig_SetVertexShaderConstantF = nullptr;

typedef HRESULT (WINAPI *SetVertexShader_fn)(IDirect3DDevice9* device, IDirect3DVertexShader9* shader);
static SetVertexShader_fn orig_SetVertexShader = nullptr;

typedef HRESULT (WINAPI *SetSamplerState_fn)(IDirect3DDevice9* device, DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value);
SetSamplerState_fn orig_SetSamplerState = nullptr;

typedef HRESULT (WINAPI *SetTextureStageState_fn)(IDirect3DDevice9* device, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);
SetTextureStageState_fn orig_SetTextureStageState = nullptr;

static bool g_vbHooksInstalled = false;



typedef HRESULT (WINAPI *Reset_fn)(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);
Reset_fn orig_Reset = nullptr;

typedef HRESULT (WINAPI *Present_fn)(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND window, const RGNDATA* dirty);
Present_fn orig_Present = nullptr;

// Cache structures
static IDirect3DBaseTexture9* g_textureCache[16] = { nullptr };

static DWORD g_renderStateCache[512] = { 0 };
static bool g_renderStateValid[512] = { false };

static DWORD g_textureStageStateCache[8][64] = { {0} };
static bool g_textureStageStateValid[8][64] = { {false} };

static DWORD g_samplerStateCache[16][32] = { {0} };
static bool g_samplerStateValid[16][32] = { {false} };

struct CachedMatrix {
    D3DMATRIX matrix;
    bool valid;
};
static CachedMatrix g_transformCache[512] = { { {0}, false } };

static D3DVIEWPORT9 g_viewportCache = { 0 };
static bool g_viewportValid = false;

struct ShadowBufferEntry {
    IDirect3DVertexBuffer9* vb;
    void* data;
    UINT size;
    bool valid;
};
static constexpr int VB_CACHE_SIZE = 128;
static constexpr int VB_CACHE_MASK = VB_CACHE_SIZE - 1;
static ShadowBufferEntry g_vbCache[VB_CACHE_SIZE] = {};

static inline unsigned int HashVB(IDirect3DVertexBuffer9* vb) {
    uintptr_t val = (uintptr_t)vb;
    return (uint32_t)((val ^ (val >> 12)) & VB_CACHE_MASK);
}

struct ConstantRegister {
    float val[4];
    bool valid;
};
static ConstantRegister g_vsConstantCache[256] = { { {0.0f}, false } };

// Latency reduction structures (Max Frame Latency = 1)
#define LATENCY_QUEUE_SIZE 2
static IDirect3DQuery9* g_latencyQueries[LATENCY_QUEUE_SIZE] = { nullptr };
static int g_latencyQueryIndex = 0;
static bool g_latencyInitialized = false;

static void InvalidateLatencyQueries(bool release) {
    for (int i = 0; i < LATENCY_QUEUE_SIZE; i++) {
        if (g_latencyQueries[i]) {
            if (release) {
                __try {
                    g_latencyQueries[i]->Release();
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
            g_latencyQueries[i] = nullptr;
        }
    }
    g_latencyInitialized = false;
    g_latencyQueryIndex = 0;
}

// Statistics
static std::atomic<long> g_textureSkips{0};
static std::atomic<long> g_renderStateSkips{0};
static std::atomic<long> g_stageStateSkips{0};
static std::atomic<long> g_samplerSkips{0};
static std::atomic<long> g_transformSkips{0};
static std::atomic<long> g_viewportSkips{0};
static std::atomic<long> g_vsConstantSkips{0};

// Clear the cache (called on Init and after device Reset)
static void CleanVBCache() {
    for (int i = 0; i < VB_CACHE_SIZE; i++) {
        if (g_vbCache[i].valid && g_vbCache[i].data) {
            VertexBufferPrealloc::FreeBuffer(g_vbCache[i].data);
            g_vbCache[i].data = nullptr;
            g_vbCache[i].valid = false;
        }
    }
}

static void InvalidateCache() {
    for (int i = 0; i < 16; i++) g_textureCache[i] = nullptr;
    for (int i = 0; i < 512; i++) g_renderStateValid[i] = false;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 64; j++) g_textureStageStateValid[i][j] = false;
    }
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 32; j++) g_samplerStateValid[i][j] = false;
    }
    for (int i = 0; i < 512; i++) g_transformCache[i].valid = false;
    g_viewportValid = false;
    for (int i = 0; i < 256; i++) g_vsConstantCache[i].valid = false;
    CleanVBCache();
}



static HRESULT WINAPI Hooked_SetRenderState(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value) {
    bool isCriticalState = (state == D3DRS_ALPHABLENDENABLE || state == D3DRS_SRCBLEND || 
                           state == D3DRS_DESTBLEND || state == D3DRS_ALPHATESTENABLE || 
                           state == D3DRS_ALPHAREF || state == D3DRS_ALPHAFUNC ||
                           state == D3DRS_ZWRITEENABLE || state == D3DRS_ZENABLE);

    if ((DWORD)state < 512 && !isCriticalState) {
        if (g_renderStateValid[state] && g_renderStateCache[state] == value) {
            g_renderStateSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_renderStateCache[state] = value;
        g_renderStateValid[state] = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetRenderState(device, state, value);
        return D3D_OK;
    }
    return orig_SetRenderState(device, state, value);
}

static HRESULT WINAPI Hooked_SetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) {
    bool isWorldTransform = (state == D3DTS_WORLD || (state >= 256 && state <= 511));

    if ((DWORD)state < 512 && matrix && !isWorldTransform) {
        if (g_transformCache[state].valid && memcmp(&g_transformCache[state].matrix, matrix, sizeof(D3DMATRIX)) == 0) {
            g_transformSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        memcpy(&g_transformCache[state].matrix, matrix, sizeof(D3DMATRIX));
        g_transformCache[state].valid = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetTransform(device, state, matrix);
        return D3D_OK;
    }
    return orig_SetTransform(device, state, matrix);
}

static HRESULT WINAPI Hooked_SetViewport(IDirect3DDevice9* device, const D3DVIEWPORT9* viewport) {
    if (viewport) {
        if (g_viewportValid && memcmp(&g_viewportCache, viewport, sizeof(D3DVIEWPORT9)) == 0) {
            g_viewportSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        memcpy(&g_viewportCache, viewport, sizeof(D3DVIEWPORT9));
        g_viewportValid = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetViewport(device, viewport);
        return D3D_OK;
    }
    return orig_SetViewport(device, viewport);
}

static HRESULT WINAPI Hooked_VB_Lock(IDirect3DVertexBuffer9* vb, UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    D3D9RenderThread::PipelineFlush();
    if (DXVKBridge::IsActive()) {
        return orig_VB_Lock(vb, OffsetToLock, SizeToLock, ppbData, Flags);
    }
    #if !TEST_DISABLE_D3D9_VB_CACHE
    if (vb && ppbData && (Flags & D3DLOCK_DISCARD)) {
        unsigned int slot = HashVB(vb);
        ShadowBufferEntry* e = &g_vbCache[slot];
        
        if (e->valid && e->vb != vb) {
            // Collision! Bypass cache to prevent heap allocation/free churn within the frame
            return orig_VB_Lock(vb, OffsetToLock, SizeToLock, ppbData, Flags);
        }
        
        if (e->valid && e->vb == vb) {
            // Recycled pointer check: make sure the cached size matches the active vertex buffer
            D3DVERTEXBUFFER_DESC desc;
            if (FAILED(vb->GetDesc(&desc)) || desc.Size != e->size) {
                if (e->data) VertexBufferPrealloc::FreeBuffer(e->data);
                e->valid = false;
                e->data = nullptr;
            }
        }
        
        if (!e->valid) {
            D3DVERTEXBUFFER_DESC desc;
            if (SUCCEEDED(vb->GetDesc(&desc))) {
                e->vb = vb;
                e->size = desc.Size;
                e->data = VertexBufferPrealloc::AllocateBuffer(desc.Size);
                e->valid = true;
            }
        }
        
        if (e->valid && e->data) {
            *ppbData = (void*)((uintptr_t)e->data + OffsetToLock);
            return D3D_OK;
        }
    }
    #endif
    return orig_VB_Lock(vb, OffsetToLock, SizeToLock, ppbData, Flags);
}


static HRESULT WINAPI Hooked_VB_Unlock(IDirect3DVertexBuffer9* vb) {
    D3D9RenderThread::PipelineFlush();
    if (DXVKBridge::IsActive()) {
        return orig_VB_Unlock(vb);
    }
    #if !TEST_DISABLE_D3D9_VB_CACHE
    if (vb) {
        unsigned int slot = HashVB(vb);
        ShadowBufferEntry* e = &g_vbCache[slot];
        if (e->valid && e->vb == vb && e->data) {
            void* realData = nullptr;
            HRESULT hr = orig_VB_Lock(vb, 0, e->size, &realData, D3DLOCK_DISCARD);
            if (SUCCEEDED(hr) && realData) {
                memcpy(realData, e->data, e->size);
                orig_VB_Unlock(vb);
            }
            return D3D_OK;
        }
    }
    #endif
    return orig_VB_Unlock(vb);
}

static HRESULT WINAPI Hooked_CreateVertexBuffer(IDirect3DDevice9* device, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) {
    HRESULT hr = orig_CreateVertexBuffer(device, Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
    if (hr == D3D_OK && ppVertexBuffer && *ppVertexBuffer && (Usage & D3DUSAGE_DYNAMIC)) {
        if (!g_vbHooksInstalled) {
            uintptr_t* vb_vtable = *(uintptr_t**)(*ppVertexBuffer);
            void* target_Lock = (void*)vb_vtable[11];
            void* target_Unlock = (void*)vb_vtable[12];
            
            if (MH_CreateHook(target_Lock, (void*)Hooked_VB_Lock, (void**)&orig_VB_Lock) == MH_OK) {
                MH_EnableHook(target_Lock);
            }
            if (MH_CreateHook(target_Unlock, (void*)Hooked_VB_Unlock, (void**)&orig_VB_Unlock) == MH_OK) {
                MH_EnableHook(target_Unlock);
            }
            g_vbHooksInstalled = true;
            Log("[D3D9StateCache] Detoured IDirect3DVertexBuffer9::Lock/Unlock for dynamic buffer optimization");
        }
    }
    return hr;
}

static HRESULT WINAPI Hooked_SetVertexShaderConstantF(IDirect3DDevice9* device, UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
    #if !TEST_DISABLE_D3D9_VS_CONSTANT_CACHE
    if (pConstantData && StartRegister + Vector4fCount <= 256) {
        bool allCached = true;
        for (UINT i = 0; i < Vector4fCount; i++) {
            UINT reg = StartRegister + i;
            if (!g_vsConstantCache[reg].valid || memcmp(g_vsConstantCache[reg].val, pConstantData + i * 4, 16) != 0) {
                allCached = false;
                break;
            }
        }
        
        if (allCached) {
            g_vsConstantSkips.fetch_add(Vector4fCount, std::memory_order_relaxed);
            return D3D_OK;
        }
        
        for (UINT i = 0; i < Vector4fCount; i++) {
            UINT reg = StartRegister + i;
            memcpy(g_vsConstantCache[reg].val, pConstantData + i * 4, 16);
            g_vsConstantCache[reg].valid = true;
        }
    }
    #endif
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetVertexShaderConstantF(device, StartRegister, pConstantData, Vector4fCount);
        return D3D_OK;
    }
    return orig_SetVertexShaderConstantF(device, StartRegister, pConstantData, Vector4fCount);
}

static HRESULT WINAPI Hooked_SetVertexShader(IDirect3DDevice9* device, IDirect3DVertexShader9* shader) {
    #if !TEST_DISABLE_D3D9_VS_CONSTANT_CACHE
    // Invalidate VS constant cache since a new shader might change register meanings or load compiler-embedded constants
    for (int i = 0; i < 256; i++) {
        g_vsConstantCache[i].valid = false;
    }
    #endif
    return orig_SetVertexShader(device, shader);
}

static HRESULT WINAPI Hooked_SetSamplerState(IDirect3DDevice9* device, DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) {
    if (Sampler < 16 && (DWORD)Type < 32) {
        if (g_samplerStateValid[Sampler][Type] && g_samplerStateCache[Sampler][Type] == Value) {
            g_samplerSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_samplerStateCache[Sampler][Type] = Value;
        g_samplerStateValid[Sampler][Type] = true;
    }

    #if !TEST_DISABLE_MIP_BIAS_GOVERNOR
    if (Type == 10 /* D3DSAMP_MIPMAPLODBIAS */) {
        float bias = MipBiasGovernor::GetCurrentBias();
        if (bias > 0.0f) {
            float floatVal = *(float*)&Value;
            floatVal += bias;
            Value = *(DWORD*)&floatVal;
        }
    }
    #endif

    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetSamplerState(device, Sampler, Type, Value);
        return D3D_OK;
    }
    return orig_SetSamplerState(device, Sampler, Type, Value);
}

static HRESULT WINAPI Hooked_SetTextureStageState(IDirect3DDevice9* device, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
    if (Stage < 8 && (DWORD)Type < 64) {
        if (g_textureStageStateValid[Stage][Type] && g_textureStageStateCache[Stage][Type] == Value) {
            g_stageStateSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_textureStageStateCache[Stage][Type] = Value;
        g_textureStageStateValid[Stage][Type] = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetTextureStageState(device, Stage, Type, Value);
        return D3D_OK;
    }
    return orig_SetTextureStageState(device, Stage, Type, Value);
}

void InvalidateAllCaches(bool safeToRelease);

static HRESULT WINAPI Hooked_Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    Log("[D3D9StateCache] Device Reset requested. Flushing render pipeline...");
    if (D3D9RenderThread::IsActive()) {
        D3D9RenderThread::PipelineFlush();
    }

    InvalidateAllCaches(true);
    FontGlyphCache::ClearCache();
    TextureUnloadDelay::Discard();
    RenderStateDedup_ClearCache();
    InterlockedIncrement(&g_deviceResetCounter);

    Log("[D3D9StateCache] Executing Reset synchronously on main thread...");
    HRESULT hr = orig_Reset(device, params);
    Log("[D3D9StateCache] Device Reset result: 0x%08X", hr);
    if (SUCCEEDED(hr)) {
        InvalidateAllCaches(true);
        FontGlyphCache::ClearCache();
        RenderStateDedup_ClearCache();
        InterlockedIncrement(&g_deviceResetCounter);
    }
    return hr;
}

static HRESULT WINAPI Hooked_Present(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND window, const RGNDATA* dirty) {
    InvalidateCache();

    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueuePresent(device, src, dest, window, dirty);
        return D3D_OK;
    }

#if !TEST_DISABLE_LOW_LATENCY_SYNC
    if (device) {
        if (!g_latencyInitialized) {
            bool ok = true;
            for (int i = 0; i < LATENCY_QUEUE_SIZE; i++) {
                HRESULT hr = device->CreateQuery(D3DQUERYTYPE_EVENT, &g_latencyQueries[i]);
                if (FAILED(hr)) {
                    ok = false;
                    g_latencyQueries[i] = nullptr;
                }
            }
            if (ok) {
                g_latencyInitialized = true;
                Log("[D3D9StateCache] Low-latency GPU sync active (MaxFrameLatency = 1)");
            } else {
                InvalidateLatencyQueries(true);
            }
        }

        if (g_latencyInitialized) {
            IDirect3DQuery9* q = g_latencyQueries[g_latencyQueryIndex];
            if (q) {
                ULONGLONG start = GetTickCount64();
                while (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
                    if (GetTickCount64() - start > 16) {
                        break;
                    }
                    SwitchToThread();
                }
            }

            IDirect3DQuery9* current_q = g_latencyQueries[g_latencyQueryIndex];
            if (current_q) {
                current_q->Issue(D3DISSUE_END);
            }

            g_latencyQueryIndex = (g_latencyQueryIndex + 1) % LATENCY_QUEUE_SIZE;
        }
    }
#endif

    return orig_Present(device, src, dest, window, dirty);
}

bool Init() {
#if TEST_DISABLE_D3D_STATE_CACHE
    Log("[D3D9StateCache] DISABLED via TEST_DISABLE_D3D_STATE_CACHE.");
    return false;
#endif


    InvalidateCache();
    Log("[D3D9StateCache] Initialized (waiting for device hooks)");
    return true;
}

void OnCreateDevice(IDirect3DDevice9* device) {
    if (!device) return;

    // Invalidate the cache whenever a new device is created to prevent stale cache entries from being reused
    InvalidateCache();

    uintptr_t* vtable = *(uintptr_t**)device;
    if (!vtable) return;

    // Always resolve the original pointers to avoid null dereferences on D3D9RenderThread
    orig_Reset = (Reset_fn)vtable[16];
    orig_Present = (Present_fn)vtable[17];
    orig_SetRenderState = (SetRenderState_fn)vtable[57];
    orig_SetTransform = (SetTransform_fn)vtable[44];
    orig_SetViewport = (SetViewport_fn)vtable[47];
    orig_CreateVertexBuffer = (CreateVertexBuffer_fn)vtable[26];
    orig_SetVertexShaderConstantF = (SetVertexShaderConstantF_fn)vtable[94];
    orig_SetSamplerState = (SetSamplerState_fn)vtable[69];
    orig_SetTextureStageState = (SetTextureStageState_fn)vtable[67];
    orig_SetVertexShader = (SetVertexShader_fn)vtable[92];

    // Only install state cache hooks if it is actually enabled by the user config
    if (!Config::g_settings.OptVulkanDXVK && !Config::g_settings.OptD3d9RenderThread) {
        return;
    }

    static bool hooksInstalled = false;
    if (hooksInstalled) return;

    void* target_Reset = (void*)orig_Reset;
    void* target_Present = (void*)orig_Present;
    void* target_SetRenderState = (void*)orig_SetRenderState;
    void* target_SetTransform = (void*)orig_SetTransform;
    void* target_SetViewport = (void*)orig_SetViewport;
    void* target_CreateVertexBuffer = (void*)orig_CreateVertexBuffer;
    void* target_SetVertexShaderConstantF = (void*)orig_SetVertexShaderConstantF;
    void* target_SetSamplerState = (void*)orig_SetSamplerState;
    void* target_SetTextureStageState = (void*)orig_SetTextureStageState;
    void* target_SetVertexShader = (void*)orig_SetVertexShader;

    if (MH_CreateHook(target_Reset, (void*)Hooked_Reset, (void**)&orig_Reset) != MH_OK ||
        MH_CreateHook(target_Present, (void*)Hooked_Present, (void**)&orig_Present) != MH_OK ||
        MH_CreateHook(target_SetRenderState, (void*)Hooked_SetRenderState, (void**)&orig_SetRenderState) != MH_OK ||
        MH_CreateHook(target_SetTransform, (void*)Hooked_SetTransform, (void**)&orig_SetTransform) != MH_OK ||
        MH_CreateHook(target_SetViewport, (void*)Hooked_SetViewport, (void**)&orig_SetViewport) != MH_OK ||
        MH_CreateHook(target_CreateVertexBuffer, (void*)Hooked_CreateVertexBuffer, (void**)&orig_CreateVertexBuffer) != MH_OK ||
        MH_CreateHook(target_SetVertexShaderConstantF, (void*)Hooked_SetVertexShaderConstantF, (void**)&orig_SetVertexShaderConstantF) != MH_OK ||
        MH_CreateHook(target_SetSamplerState, (void*)Hooked_SetSamplerState, (void**)&orig_SetSamplerState) != MH_OK ||
        MH_CreateHook(target_SetTextureStageState, (void*)Hooked_SetTextureStageState, (void**)&orig_SetTextureStageState) != MH_OK ||
        MH_CreateHook(target_SetVertexShader, (void*)Hooked_SetVertexShader, (void**)&orig_SetVertexShader) != MH_OK) 
    {
        Log("[D3D9StateCache] Failed to create MinHook detours");
        return;
    }

    MH_EnableHook(target_Reset);
    MH_EnableHook(target_Present);
    MH_EnableHook(target_SetRenderState);
    MH_EnableHook(target_SetTransform);
    MH_EnableHook(target_SetViewport);
    MH_EnableHook(target_CreateVertexBuffer);
    MH_EnableHook(target_SetVertexShaderConstantF);
    MH_EnableHook(target_SetSamplerState);
    MH_EnableHook(target_SetTextureStageState);
    MH_EnableHook(target_SetVertexShader);

    hooksInstalled = true;
    Log("[D3D9StateCache] Active - Redundant render state filtering successfully hooked on main thread");
}

void Shutdown() {
    InvalidateLatencyQueries(false);
    CleanVBCache();
    Log("[D3D9StateCache] Redundancy Skips: Textures: %ld, RenderStates: %ld, StageStates: %ld, Samplers: %ld, Transforms: %ld, Viewports: %ld, VSConstants: %ld",
        g_textureSkips.load(), g_renderStateSkips.load(), g_stageStateSkips.load(), g_samplerSkips.load(), g_transformSkips.load(), g_viewportSkips.load(), g_vsConstantSkips.load());
}

void InvalidateAllCaches(bool safeToRelease) {
    Log("[D3D9StateCache] Clearing all state cache registries (device change/reset, safeToRelease=%d)...", safeToRelease);
    InvalidateCache();
    InvalidateLatencyQueries(safeToRelease);
    CleanVBCache();
}

} // namespace D3D9StateCache
