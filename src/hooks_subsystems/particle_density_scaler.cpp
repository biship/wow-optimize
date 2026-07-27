#include "particle_density_scaler.h"
#include "version.h"
#include <cstdio>
#include <cmath>

extern "C" void Log(const char* fmt, ...);

namespace ParticleDensityScaler {

typedef char (__thiscall *CVar_Set_fn)(void* This, const char* value, char a3, char a4, char a5, char a6);
static CVar_Set_fn orig_CVar_Set = (CVar_Set_fn)0x007668C0;

static void* g_particleDensityCVar = nullptr;
static float g_currentFps = 60.0f;
static float g_targetDensity = 1.0f;
static float g_actualDensity = 1.0f;

void RegisterCVar(const char* name, void* cvar) {
    if (name && strcmp(name, "particleDensity") == 0) {
        g_particleDensityCVar = cvar;
        Log("[ParticleDensityScaler] Captured particleDensity CVar pointer: 0x%p", g_particleDensityCVar);
    }
}

} // namespace ParticleDensityScaler

extern "C" void RegisterParticleCVar(const char* name, void* cvar) {
    ParticleDensityScaler::RegisterCVar(name, cvar);
}

namespace ParticleDensityScaler {

bool Init() {
    Log("[ParticleDensityScaler] Active - Particle Density Dynamic Scaler initialized");
    return true;
}

void Shutdown() {
    // No-op
}

void OnFrame(float elapsedMs) {
    if (elapsedMs <= 0.0f || !g_particleDensityCVar) return;

    float fps = 1000.0f / elapsedMs;
    g_currentFps = g_currentFps * 0.98f + fps * 0.02f;

    // Adapt target density based on current FPS
    if (g_currentFps < 35.0f) {
        g_targetDensity = 0.1f; // Drop particles to 10%
    } else if (g_currentFps < 50.0f) {
        g_targetDensity = 0.4f; // Drop particles to 40%
    } else if (g_currentFps > 58.0f) {
        g_targetDensity = 1.0f; // Restore particles to 100%
    }

    if (std::abs(g_targetDensity - g_actualDensity) >= 0.1f) {
        g_actualDensity = g_targetDensity;
        char buf[16];
        sprintf(buf, "%.2f", g_actualDensity);
        __try {
            orig_CVar_Set(g_particleDensityCVar, buf, 1, 0, 0, 0);
            Log("[ParticleDensityScaler] Scaling particle density to: %.2f (FPS: %.1f)", g_actualDensity, g_currentFps);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}


} // namespace ParticleDensityScaler
