#pragma once
#include <windows.h>
#include <cstring>

namespace ParticleDensityScaler {
    // Records the particleDensity CVar object when the client registers it.
    void RegisterCVar(const char* name, void* cvar);
}
