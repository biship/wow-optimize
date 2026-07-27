// ============================================================================
// Module: sound_update_guard.cpp
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include "sound_update_guard.h"
#include "sound_buffer_guard.h"

extern "C" void Log(const char* fmt, ...);

bool InstallSoundUpdateGuard()
{
#if TEST_DISABLE_SOUND_UPDATE_GUARD
    Log("[SndUpdate] DISABLED via feature flag");
    return false;
#else
    void* target = (void*)0x00508320;
    unsigned char* p = (unsigned char*)target;

    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        // Already hooked by sound_buffer_guard — just register the feature
        CrashDumper::RegisterFeature("SndUpdate");
        CrashDumper::FeatureSetActive("SndUpdate", true);
        Log("[SndUpdate] ACTIVE: sharing sub_508320 SEH guard with SndBuffer");
        return true;
    }

    // Not hooked yet — delegate full install to sound_buffer_guard
    Log("[SndUpdate] sub_508320 not yet hooked, delegating to SndBuffer guard");
    return InstallSoundBufferGuard();
#endif
}

void UninstallSoundUpdateGuard()
{
#if !TEST_DISABLE_SOUND_UPDATE_GUARD
    CrashDumper::FeatureSetActive("SndUpdate", false);
    // sub_508320 hook is owned+uninstalled by sound_buffer_guard
#endif
}
