#include "sound_coalescer.h"
#include "sound_volume_limit.h"
#include "MinHook.h"
#include "version.h"
#include <unordered_map>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace SoundCoalescer {

typedef int (APIENTRY *FSOUND_PlaySound_fn)(int channel, void* sptr);
static FSOUND_PlaySound_fn orig_FSOUND_PlaySound = nullptr;

typedef int (APIENTRY *FSOUND_PlaySoundEx_fn)(int channel, void* sptr, void* dsp, signed char startpaused);
static FSOUND_PlaySoundEx_fn orig_FSOUND_PlaySoundEx = nullptr;

// NOTE: this used to hard-drop any replay of the same FMOD sample within 30ms
// (return -1 to skip the play). That silently ate legitimate sounds - boss
// voice lines and any sound that reuses a sample slot - which is what testers
// reported as "can't hear boss voices". It was also keyed on a raw sample
// pointer that FMOD frees and reuses (stale-key hazard) and never pruned the
// map (unbounded leak).
//
// The anti-clipping intent is fully covered by SoundVolumeLimit, which
// attenuates the same sound when it overlaps on >2 channels (floor 32/255,
// never silent). So we no longer drop anything - we always let the sound play
// and just feed the volume limiter. This can never silence a distinct sound.

static int APIENTRY Hooked_FSOUND_PlaySound(int channel, void* sptr) {
    int res = orig_FSOUND_PlaySound(channel, sptr);
    if (res >= 0) {
        ::SoundVolumeLimit::LimitChannelVolume(res, sptr);
    }
    return res;
}

static int APIENTRY Hooked_FSOUND_PlaySoundEx(int channel, void* sptr, void* dsp, signed char startpaused) {
    int res = orig_FSOUND_PlaySoundEx(channel, sptr, dsp, startpaused);
    if (res >= 0) {
        ::SoundVolumeLimit::LimitChannelVolume(res, sptr);
    }
    return res;
}

bool Init() {
    HMODULE hFmod = GetModuleHandleA("fmod.dll");
    if (!hFmod) {
        Log("[SoundCoalescer] fmod.dll not loaded yet");
        return false;
    }

    void* pPlay = (void*)GetProcAddress(hFmod, "FSOUND_PlaySound");
    void* pPlayEx = (void*)GetProcAddress(hFmod, "FSOUND_PlaySoundEx");

    if (pPlay && MH_CreateHook(pPlay, (void*)Hooked_FSOUND_PlaySound, (void**)&orig_FSOUND_PlaySound) == MH_OK) {
        MH_EnableHook(pPlay);
        Log("[SoundCoalescer] Hooked FSOUND_PlaySound successfully");
    }

    if (pPlayEx && MH_CreateHook(pPlayEx, (void*)Hooked_FSOUND_PlaySoundEx, (void**)&orig_FSOUND_PlaySoundEx) == MH_OK) {
        MH_EnableHook(pPlayEx);
        Log("[SoundCoalescer] Hooked FSOUND_PlaySoundEx successfully");
    }

    Log("[SoundCoalescer] Active - Advanced Sound Channels Coalescer initialized");
    return true;
}

void Shutdown() {
    // Nothing to tear down: we no longer keep any per-sample state. The hooks
    // stay installed for process lifetime (same as before).
}

} // namespace SoundCoalescer
