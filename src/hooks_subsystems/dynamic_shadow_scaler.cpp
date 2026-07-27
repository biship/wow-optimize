#include "dynamic_shadow_scaler.h"
#include "version.h"
#include <cstdio>
#include <cmath>

extern "C" void Log(const char* fmt, ...);

namespace DynamicShadowScaler {

typedef char (__thiscall *CVar_Set_fn)(void* This, const char* value, char a3, char a4, char a5, char a6);
static CVar_Set_fn orig_CVar_Set = (CVar_Set_fn)0x007668C0;

static void* g_shadowQualityCVar = nullptr;
static float g_currentFps = 60.0f;
static int g_targetLevel = 3; // 3 = Ultra/High, 0 = Off/Low
static int g_actualLevel = 3;

void RegisterCVar(const char* name, void* cvar) {
    if (name && (strcmp(name, "extShadowQuality") == 0 || strcmp(name, "shadowLevel") == 0)) {
        g_shadowQualityCVar = cvar;
        Log("[DynamicShadowScaler] Captured shadow CVar pointer: %s -> 0x%p", name, g_shadowQualityCVar);
    }
}

} // namespace DynamicShadowScaler

extern "C" void RegisterShadowCVar(const char* name, void* cvar) {
    DynamicShadowScaler::RegisterCVar(name, cvar);
}

namespace DynamicShadowScaler {

bool Init() {
    Log("[DynamicShadowScaler] Active - Dynamic Shadow Quality Auto-Scaler initialized");
    return true;
}

void Shutdown() {
    // No-op
}

void OnFrame(float elapsedMs) {
    if (elapsedMs <= 0.0f || !g_shadowQualityCVar) return;

    float fps = 1000.0f / elapsedMs;
    g_currentFps = g_currentFps * 0.98f + fps * 0.02f;

    // Adapt target shadow level based on current FPS
    if (g_currentFps < 40.0f) {
        g_targetLevel = 0; // Drop shadows completely to recover frame rate
    } else if (g_currentFps < 50.0f) {
        g_targetLevel = 1; // Medium shadows
    } else if (g_currentFps > 58.0f) {
        g_targetLevel = 3; // Max shadows
    }

    if (g_targetLevel != g_actualLevel) {
        g_actualLevel = g_targetLevel;
        char buf[16];
        sprintf(buf, "%d", g_actualLevel);
        __try {
            orig_CVar_Set(g_shadowQualityCVar, buf, 1, 0, 0, 0);
            Log("[DynamicShadowScaler] Scaling shadows to level: %d (FPS: %.1f)", g_actualLevel, g_currentFps);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

} // namespace DynamicShadowScaler
