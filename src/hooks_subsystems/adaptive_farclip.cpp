#include "adaptive_farclip.h"
#include "MinHook.h"
#include "version.h"
#include <cstdio>
#include <cmath>

extern "C" void Log(const char* fmt, ...);
extern "C" void RegisterParticleCVar(const char* name, void* cvar);
extern "C" void RegisterNameplateCVar(const char* name, void* cvar);

namespace AdaptiveFarclip {

typedef void* (__cdecl *CVar_Register_fn)(const char* name, const char* defaultValue, int flags, const char* help, void* callback, int category, bool a7, int a8, bool a9);
static CVar_Register_fn orig_CVar_Register = nullptr;

typedef char (__thiscall *CVar_Set_fn)(void* This, const char* value, char a3, char a4, char a5, char a6);
static CVar_Set_fn orig_CVar_Set = (CVar_Set_fn)0x007668C0;

static void* g_farclipCVar = nullptr;
static float g_currentFps = 60.0f;
static float g_targetFarclip = 1250.0f;
static float g_actualFarclip = 1250.0f;

static void* __cdecl Hooked_CVar_Register(const char* name, const char* defaultValue, int flags, const char* help, void* callback, int category, bool a7, int a8, bool a9) {
    void* cvar = orig_CVar_Register(name, defaultValue, flags, help, callback, category, a7, a8, a9);
    if (cvar && name) {
        if (strcmp(name, "farclip") == 0) {
            g_farclipCVar = cvar;
            Log("[AdaptiveFarclip] Captured farclip CVar pointer: 0x%p", g_farclipCVar);
        }
        RegisterParticleCVar(name, cvar);
        RegisterNameplateCVar(name, cvar);
    }
    return cvar;
}

bool Init() {
    #if TEST_DISABLE_ADAPTIVE_FARCLIP
    Log("[AdaptiveFarclip] DISABLED via configuration");
    return false;
    #else
    void* target = (void*)0x00767FC0;
    
    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[AdaptiveFarclip] Target 0x00767FC0 not readable.");
        return false;
    }

    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[AdaptiveFarclip] BAD PROLOGUE at 0x00767FC0. Skipping hook.");
        return false;
    }

    if (MH_CreateHook(target, (void*)Hooked_CVar_Register, (void**)&orig_CVar_Register) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[AdaptiveFarclip] Hooked CVar::Register successfully");
            return true;
        }
        MH_RemoveHook(target);
    }
    return false;
    #endif
}

void Shutdown() {
    #if !TEST_DISABLE_ADAPTIVE_FARCLIP
    MH_DisableHook((void*)0x00767FC0);
    #endif
}

void OnFrame(float elapsedMs) {
    #if !TEST_DISABLE_ADAPTIVE_FARCLIP
    if (elapsedMs <= 0.0f || !g_farclipCVar) return;

    float fps = 1000.0f / elapsedMs;
    // Smooth the FPS reading to avoid rapid fluctuations
    g_currentFps = g_currentFps * 0.98f + fps * 0.02f;

    // Adapt target farclip based on current FPS
    if (g_currentFps < 55.0f) {
        // Drop farclip to reduce rendering load
        g_targetFarclip -= 10.0f;
        if (g_targetFarclip < 185.0f) g_targetFarclip = 185.0f; // minimum WoW farclip
    } else if (g_currentFps > 58.0f) {
        // Gradually recover farclip
        g_targetFarclip += 2.0f;
        if (g_targetFarclip > 1250.0f) g_targetFarclip = 1250.0f; // default high farclip
    }

    // Only update CVar when the delta is notable (e.g. step of 50 units) to avoid setting CVar too often
    if (std::abs(g_targetFarclip - g_actualFarclip) >= 50.0f) {
        g_actualFarclip = g_targetFarclip;
        char buf[32];
        sprintf(buf, "%d", (int)g_actualFarclip);
        // Force the CVar value update
        orig_CVar_Set(g_farclipCVar, buf, 1, 0, 0, 0);
    }
    #endif
}

} // namespace AdaptiveFarclip
