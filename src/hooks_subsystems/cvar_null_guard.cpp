// ============================================================================
// Module: cvar_null_guard.cpp
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "quality_governor.h"
#include "cvar_null_guard.h"

extern "C" void Log(const char* fmt, ...);

#include <string.h>

typedef char (__fastcall *Sub7668C0_t)(void* ecx, void* edx, char* Str1, char a3, char a4, char a5, char a6);
static Sub7668C0_t g_orig7668C0 = nullptr;

static char __fastcall Hooked_7668C0(void* ecx, void* edx, char* Str1, char a3, char a4, char a5, char a6)
{
    uintptr_t p = (uintptr_t)ecx;
    if (p < 0x10000 || p > 0xFFE00000) {
        return 1;
    }
    uintptr_t ptrAt104 = *(uintptr_t*)((char*)ecx + 104);
    if (ptrAt104 != 0 && (ptrAt104 < 0x10000 || ptrAt104 > 0xFFE00000)) {
        return 1;
    }

#if !TEST_DISABLE_TIMING_FIX
    if (ecx && Str1) {
        const char* name = *(const char**)((char*)ecx + 20);
        if (name && (uintptr_t)name >= 0x10000 && (uintptr_t)name <= 0xFFE00000) {
            char c0 = name[0];
            if (c0 == 't' || c0 == 'T') {
                if (_stricmp(name, "timingMethod") == 0) {
                    Str1 = (char*)"2";
                } else if (_stricmp(name, "timingTestError") == 0) {
                    Str1 = (char*)"0";
                }
            }
        }
    }
#endif

    // Every CVar the client sets passes here, which is the only place anything
    // can learn what the player actually chose.
    if (ecx && Str1) {
        const char* nm = *(const char**)((char*)ecx + 20);
        if (nm && (uintptr_t)nm >= 0x10000 && (uintptr_t)nm <= 0xFFE00000) {
            QualityGovernor::NoteCVarObject(ecx, nm);
            QualityGovernor::NoteCVarWrite(nm, Str1);
        }
    }

    return g_orig7668C0(ecx, edx, Str1, a3, a4, a5, a6);
}

bool InstallCvarNullGuard()
{
    void* target = (void*)0x007668C0;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        Log("[CvarGuard] BAD PROLOGUE (got %02X %02X %02X)", p[0], p[1], p[2]);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_7668C0, (void**)&g_orig7668C0) != MH_OK) {
        Log("[CvarGuard] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[CvarGuard] MH_EnableHook FAILED");
        return false;
    }
    Log("[CvarGuard] ACTIVE -- NULL this guard on sub_7668C0");
    return true;
}
