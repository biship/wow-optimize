// ============================================================================
// Module: render_null_guard.cpp
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "render_null_guard.h"

extern "C" void Log(const char* fmt, ...);

// Global pointers that sub_873060 reads
static uint32_t* const D43020 = (uint32_t*)0x00D43020; // device-ready flag
static uint32_t* const D43024 = (uint32_t*)0x00D43024; // device state pointer
static uint32_t* const C5DF88 = (uint32_t*)0x00C5DF88; // global render device

// Original function
typedef int (__cdecl *Sub873060_t)(int a1, int a2);
static Sub873060_t g_orig873060 = nullptr;

static bool IsDeviceReady() {
    uintptr_t pGxDevice = *(uintptr_t*)0x00C5DF88;
    if (pGxDevice < 0x10000 || pGxDevice > 0xFFE00000) return false;
    
    uintptr_t pD3d9Device = *(uintptr_t*)(pGxDevice + 0x397C);
    if (pD3d9Device < 0x10000 || pD3d9Device > 0xFFE00000) return false;
    
    uintptr_t pVtable = *(uintptr_t*)pD3d9Device;
    if (pVtable < 0x10000 || pVtable > 0xFFE00000) return false;
    
    return true;
}

static int __cdecl Hooked_873060(int a1, int a2)
{
    // If the device state pointer hasn't been initialized yet
    // (sub_872F90 hasn't run), the function has nothing to configure.
    // Return 1 (original's success code), not 0, to avoid triggering
    // error paths in callers.
    if (!*D43024 || !*C5DF88 || !IsDeviceReady()) {
        return 1;  // success — nothing to do
    }

    return g_orig873060(a1, a2);
}

bool InstallRenderNullGuard()
{
    void* target = (void*)0x00873060;
    unsigned char* p = (unsigned char*)target;

    // Verify prologue: push ebp; mov ebp, esp
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        Log("[RenderGuard] BAD PROLOGUE at 0x%08X (got %02X %02X %02X)",
            (uintptr_t)target, p[0], p[1], p[2]);
        return false;
    }

    if (MH_CreateHook(target, (void*)Hooked_873060, (void**)&g_orig873060) != MH_OK) {
        Log("[RenderGuard] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[RenderGuard] MH_EnableHook FAILED");
        return false;
    }

    Log("[RenderGuard] ACTIVE v2 -- guards dword_D43024 + dword_C5DF88 at sub_873060");
    return true;
}

