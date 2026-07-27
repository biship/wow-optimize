#include "config.h"
#include <windows.h>
#include <string>
#include <cstdio>
#include <cstring>

extern "C" void Log(const char* fmt, ...);

namespace Config {
    Settings g_settings;

    // Which file the settings above actually came from. A bug report is only as
    // good as knowing what was switched on, and twice now a log and the ini sent
    // with it disagreed - once because a launcher button had overridden a switch,
    // once with no explanation available at all, because nothing recorded where
    // the DLL had read from or what it concluded.
    static std::string g_loadedFrom;

    void DumpToLog() {
        Log("[Config] Read from: %s", g_loadedFrom.empty() ? "(none)" : g_loadedFrom.c_str());

        FILE* f = fopen(g_loadedFrom.c_str(), "r");
        if (!f) {
            Log("[Config] File could not be reopened for the dump - the settings "
                "above are whatever the defaults are.");
            return;
        }
        Log("[Config] Effective contents:");
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            size_t n = strlen(p);
            while (n && (p[n-1] == '\n' || p[n-1] == '\r' || p[n-1] == ' ')) p[--n] = 0;
            if (!*p || *p == ';' || *p == '#') continue;
            Log("[Config]   %s", p);
        }
        fclose(f);
    }

    void Load() {
        std::string iniPath;

        // Explicit config path wins (set by wow_loader --config, or anyone who
        // wants to point us at a specific profile). Only honored if it exists,
        // so a bad path falls back to the normal wow_opt.ini instead of writing
        // a fresh default somewhere unexpected.
        char envPath[MAX_PATH];
        DWORD envLen = GetEnvironmentVariableA("WOW_OPT_CONFIG", envPath, MAX_PATH);
        if (envLen > 0 && envLen < MAX_PATH &&
            GetFileAttributesA(envPath) != INVALID_FILE_ATTRIBUTES) {
            iniPath = envPath;
        } else {
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            iniPath = path;
            size_t lastSlash = iniPath.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                iniPath = iniPath.substr(0, lastSlash + 1) + "wow_opt.ini";
            } else {
                iniPath = "wow_opt.ini";
            }
        }

        // Check if the file exists, if not, write the default template
        DWORD attribs = GetFileAttributesA(iniPath.c_str());
        if (attribs == INVALID_FILE_ATTRIBUTES) {
            // Write default ini template with safe defaults (mostly 0 except core/stable guards)
            // General
            WritePrivateProfileStringA("General", "SleepPrecision", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "SleepPrecisionValue", "8", iniPath.c_str());
            WritePrivateProfileStringA("General", "SessionLogs", "1", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaStackFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "SessionLogsToKeep", "10", iniPath.c_str());
            WritePrivateProfileStringA("General", "MemoryPressure", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "HeapCompactor", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "DefragLf", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "VulkanDXVK", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "TimingFix", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "CvarNullGuard", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "FrameLimiter", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "MpqMmapVfs", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "MpqPrefetch", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "ObjVisCache", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "DbcPreload", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "OomGovernor", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "HardwareCursor", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "SamplingProfiler", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "MimallocLarge", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "VaArena", "0", iniPath.c_str());  // EXPERIMENTAL, opt-in
            WritePrivateProfileStringA("General", "CompatMode", "0", iniPath.c_str());  // set 1 on VMs/HyperV if the game can't connect


            // UI & Lua
            WritePrivateProfileStringA("UI_Lua", "UIFrameBatch", "0", iniPath.c_str());  // #36: artifacting/flicker — off by default
            WritePrivateProfileStringA("UI_Lua", "AddonDispatcher", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "UIFrameAccessorFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FontMetricsFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "ModuleHandleCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FrameScriptDispatch", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaNumConvFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaOpcache", "0", iniPath.c_str());  // #37: slow loads + Lua errors — off by default
            WritePrivateProfileStringA("UI_Lua", "LuaGcCoalesce", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaJIT", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaGetTimeFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "AsyncTexLoader", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "AsyncTerrainLoader", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "RcuObjMgr", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "MipBiasGovernor", "0", iniPath.c_str());

            // Combat & Network
            WritePrivateProfileStringA("Combat_Net", "CombatLogLeakFix", "1", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "CombatLogParser", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "CombatLogIncremental", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "EventCoalescer", "0", iniPath.c_str());  // #42: hides buffs/countdowns — off by default
            WritePrivateProfileStringA("Combat_Net", "SavedVarsSerializer", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "SavedVarsAsync", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "SavedVarsPretoken", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "UnitAuraFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NetworkGuidSse2", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "GetSpellInfoCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "PacketOffload", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NameplateMT", "0", iniPath.c_str());

            // Graphics & Sound
            WritePrivateProfileStringA("Graphics_Sound", "StrStrSse2", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "StrCatFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "SoundMixerOpt", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "AudioDecodeMt", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "DbcLookupCache", "0", iniPath.c_str());  // #35: crash while loading — off by default
            WritePrivateProfileStringA("Graphics_Sound", "WorldStateCoalesce", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "D3d9RenderThread", "0", iniPath.c_str());

            // 10 new features defaults
            WritePrivateProfileStringA("Combat_Net", "CombatLogFilter", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "SoundVolumeLimit", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "UILayoutThrottle", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "TerrainHeightCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "AnimBlendCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "SavedVarsOpt", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "ItemDataPrefetch", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "MovementSmoothing", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FontAlphaFastpath", "0", iniPath.c_str());

            WritePrivateProfileStringA("Combat_Net", "PacketProcessingThrottle", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NameplateCulling", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "TextureUnloadDelay", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "M2MatrixSimd", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "M2BoneMt", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "MpqAsyncDecompress", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "SimdMatrixTransform", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "MinimapRefreshGovernor", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "SpellEffectCulling", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaStringCompareFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "DbcRowCaching", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NetworkStringDedup", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "SoundFreqCoalesce", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "AuraUpdateDedup", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "UiTextureCaching", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "WmoCullingOpt", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FastFloatParse", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "HeapAllocationTracker", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "CrtMimalloc", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "SpellCooldownCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "GuidStringCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FrameScriptMemOpt", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "CombatEventLimit", "0", iniPath.c_str());
        }

        // Read all settings
        // General
        g_settings.OptSleepPrecision      = GetPrivateProfileIntA("General", "SleepPrecision", 1, iniPath.c_str()) != 0;
        g_settings.SleepPrecisionValue    = GetPrivateProfileIntA("General", "SleepPrecisionValue", 8, iniPath.c_str());
        g_settings.OptSessionLogs         = GetPrivateProfileIntA("General", "SessionLogs", 1, iniPath.c_str()) != 0;
        g_settings.OptLuaStackFast        = GetPrivateProfileIntA("UI_Lua", "LuaStackFast", 0, iniPath.c_str()) != 0;
        g_settings.SessionLogsToKeep      = GetPrivateProfileIntA("General", "SessionLogsToKeep", 10, iniPath.c_str());
        g_settings.OptMemoryPressure      = GetPrivateProfileIntA("General", "MemoryPressure", 1, iniPath.c_str()) != 0;
        g_settings.OptHeapCompactor       = GetPrivateProfileIntA("General", "HeapCompactor", 1, iniPath.c_str()) != 0;
        g_settings.OptDefragLf            = GetPrivateProfileIntA("General", "DefragLf", 0, iniPath.c_str()) != 0;
        g_settings.OptVulkanDXVK          = GetPrivateProfileIntA("General", "VulkanDXVK", 0, iniPath.c_str()) != 0;
        g_settings.OptTimingFix           = GetPrivateProfileIntA("General", "TimingFix", 0, iniPath.c_str()) != 0;
        g_settings.OptCvarNullGuard       = GetPrivateProfileIntA("General", "CvarNullGuard", 1, iniPath.c_str()) != 0;
        g_settings.OptFrameLimiter        = GetPrivateProfileIntA("General", "FrameLimiter", 0, iniPath.c_str()) != 0;
        g_settings.OptMpqMmapVfs          = GetPrivateProfileIntA("General", "MpqMmapVfs", 0, iniPath.c_str()) != 0;
        g_settings.OptMpqPrefetch         = GetPrivateProfileIntA("General", "MpqPrefetch", 0, iniPath.c_str()) != 0;
        g_settings.OptObjVisCache         = GetPrivateProfileIntA("General", "ObjVisCache", 1, iniPath.c_str()) != 0;
        g_settings.OptDbcPreload          = GetPrivateProfileIntA("General", "DbcPreload", 0, iniPath.c_str()) != 0;
        g_settings.OptOomGovernor         = GetPrivateProfileIntA("General", "OomGovernor", 0, iniPath.c_str()) != 0;
        g_settings.OptHardwareCursor      = GetPrivateProfileIntA("General", "HardwareCursor", 0, iniPath.c_str()) != 0;
        g_settings.OptSamplingProfiler    = GetPrivateProfileIntA("General", "SamplingProfiler", 0, iniPath.c_str()) != 0;
        g_settings.OptMimallocLarge       = GetPrivateProfileIntA("General", "MimallocLarge", 0, iniPath.c_str()) != 0;
        g_settings.OptVaArena             = GetPrivateProfileIntA("General", "VaArena", 0, iniPath.c_str()) != 0;
        g_settings.OptCompatMode          = GetPrivateProfileIntA("General", "CompatMode", 0, iniPath.c_str()) != 0;
        // HARD-DISABLED regardless of ini: in tester logs the arena was active
        // on machines with zero fragmentation (2GB+ largest free block), so it
        // used ~0.2MB of its 64MB and delivered no benefit - while still routing
        // EVERY process VirtualAlloc/VirtualFree through our global hook + lock.
        // It correlated with repeated 10-14s main-thread freezes and an
        // exit-time crash, and it is unproven. Keep the (corrected) code for a
        // future, fragmentation-gated retry, but do not activate it now.
        g_settings.OptVaArena = false;


        // UI & Lua
        g_settings.OptUIFrameBatch        = GetPrivateProfileIntA("UI_Lua", "UIFrameBatch", 0, iniPath.c_str()) != 0;
        g_settings.OptAddonDispatcher     = GetPrivateProfileIntA("UI_Lua", "AddonDispatcher", 0, iniPath.c_str()) != 0;
        g_settings.OptUIFrameAccessorFast = GetPrivateProfileIntA("UI_Lua", "UIFrameAccessorFast", 0, iniPath.c_str()) != 0;
        g_settings.OptFontMetricsFast     = GetPrivateProfileIntA("UI_Lua", "FontMetricsFast", 0, iniPath.c_str()) != 0;
        g_settings.OptModuleHandleCache   = GetPrivateProfileIntA("UI_Lua", "ModuleHandleCache", 0, iniPath.c_str()) != 0;
        g_settings.OptFrameScriptDispatch = GetPrivateProfileIntA("UI_Lua", "FrameScriptDispatch", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaNumConvFast      = GetPrivateProfileIntA("UI_Lua", "LuaNumConvFast", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaOpcache          = GetPrivateProfileIntA("UI_Lua", "LuaOpcache", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaGcCoalesce       = GetPrivateProfileIntA("UI_Lua", "LuaGcCoalesce", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaJIT              = GetPrivateProfileIntA("UI_Lua", "LuaJIT", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaGetTimeFast      = GetPrivateProfileIntA("UI_Lua", "LuaGetTimeFast", 0, iniPath.c_str()) != 0;
        g_settings.OptAsyncTexLoader      = GetPrivateProfileIntA("UI_Lua", "AsyncTexLoader", 0, iniPath.c_str()) != 0;
        g_settings.OptAsyncTerrainLoader  = GetPrivateProfileIntA("UI_Lua", "AsyncTerrainLoader", 0, iniPath.c_str()) != 0;
        g_settings.OptRcuObjMgr           = GetPrivateProfileIntA("UI_Lua", "RcuObjMgr", 0, iniPath.c_str()) != 0;
        g_settings.OptMipBiasGovernor     = GetPrivateProfileIntA("UI_Lua", "MipBiasGovernor", 0, iniPath.c_str()) != 0;

        // Combat & Network
        g_settings.OptCombatLogLeakFix    = GetPrivateProfileIntA("Combat_Net", "CombatLogLeakFix", 1, iniPath.c_str()) != 0;
        g_settings.OptCombatLogParser     = GetPrivateProfileIntA("Combat_Net", "CombatLogParser", 0, iniPath.c_str()) != 0;
        g_settings.OptCombatLogIncremental = GetPrivateProfileIntA("Combat_Net", "CombatLogIncremental", 0, iniPath.c_str()) != 0;
        g_settings.OptEventCoalescer      = GetPrivateProfileIntA("Combat_Net", "EventCoalescer", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsSerializer = GetPrivateProfileIntA("Combat_Net", "SavedVarsSerializer", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsAsync      = GetPrivateProfileIntA("Combat_Net", "SavedVarsAsync", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsPretoken   = GetPrivateProfileIntA("Combat_Net", "SavedVarsPretoken", 0, iniPath.c_str()) != 0;
        g_settings.OptUnitAuraFast        = GetPrivateProfileIntA("Combat_Net", "UnitAuraFast", 0, iniPath.c_str()) != 0;
        g_settings.OptNetworkGuidSse2     = GetPrivateProfileIntA("Combat_Net", "NetworkGuidSse2", 0, iniPath.c_str()) != 0;
        g_settings.OptGetSpellInfoCache   = GetPrivateProfileIntA("Combat_Net", "GetSpellInfoCache", 0, iniPath.c_str()) != 0;
        g_settings.OptPacketOffload       = GetPrivateProfileIntA("Combat_Net", "PacketOffload", 0, iniPath.c_str()) != 0;
        g_settings.OptNameplateMT         = GetPrivateProfileIntA("Combat_Net", "NameplateMT", 0, iniPath.c_str()) != 0;

        // Graphics & Sound
        g_settings.OptFastMemsetOpt       = GetPrivateProfileIntA("Graphics_Sound", "FastMemsetOpt", 1, iniPath.c_str()) != 0;
        g_settings.OptFastStrnicmpOpt     = GetPrivateProfileIntA("UI_Lua", "FastStrnicmpOpt", 1, iniPath.c_str()) != 0;
        g_settings.OptLuaSNewLstrFast     = GetPrivateProfileIntA("UI_Lua", "LuaSNewLstrFast", 0, iniPath.c_str()) != 0;
        g_settings.OptStrStrSse2          = GetPrivateProfileIntA("Graphics_Sound", "StrStrSse2", 0, iniPath.c_str()) != 0;
        g_settings.OptStrCatFast          = GetPrivateProfileIntA("Graphics_Sound", "StrCatFast", 0, iniPath.c_str()) != 0;
        g_settings.OptSoundMixerOpt       = GetPrivateProfileIntA("Graphics_Sound", "SoundMixerOpt", 0, iniPath.c_str()) != 0;
        g_settings.OptAudioDecodeMt       = GetPrivateProfileIntA("Graphics_Sound", "AudioDecodeMt", 0, iniPath.c_str()) != 0;
        g_settings.OptDbcLookupCache      = GetPrivateProfileIntA("Graphics_Sound", "DbcLookupCache", 0, iniPath.c_str()) != 0;
        g_settings.OptWorldStateCoalesce  = GetPrivateProfileIntA("Graphics_Sound", "WorldStateCoalesce", 0, iniPath.c_str()) != 0;
        g_settings.OptD3d9RenderThread    = GetPrivateProfileIntA("Graphics_Sound", "D3d9RenderThread", 0, iniPath.c_str()) != 0;
        // HARD-DISABLED regardless of ini: this offloads D3D9 draw/Present/Reset
        // calls to a worker thread, but WoW's device isn't created
        // D3DCREATE_MULTITHREADED, so cross-thread rendering is undefined
        // behavior -> mid-fight screen flashing, and the main thread spin-waits
        // in PipelineFlush on a render thread that stalls on the device Reset
        // during zone loads -> the "loaded then froze forever" hang. Same class
        // of unsafe-by-design as MimallocLarge. Kept readable above only so old
        // profiles with D3d9RenderThread=1 don't silently re-enable it.
        g_settings.OptD3d9RenderThread = false;

        // Parse Features 21-30
        g_settings.OptCombatLogFilter    = GetPrivateProfileIntA("Combat_Net", "CombatLogFilter", 0, iniPath.c_str()) != 0;
        g_settings.OptSoundVolumeLimit   = GetPrivateProfileIntA("Graphics_Sound", "SoundVolumeLimit", 0, iniPath.c_str()) != 0;
        g_settings.OptUILayoutThrottle   = GetPrivateProfileIntA("UI_Lua", "UILayoutThrottle", 0, iniPath.c_str()) != 0;
        g_settings.OptTerrainHeightCache = GetPrivateProfileIntA("Graphics_Sound", "TerrainHeightCache", 0, iniPath.c_str()) != 0;
        g_settings.OptAnimBlendCache     = GetPrivateProfileIntA("Graphics_Sound", "AnimBlendCache", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsOpt       = GetPrivateProfileIntA("General", "SavedVarsOpt", 0, iniPath.c_str()) != 0;
        g_settings.OptItemDataPrefetch   = GetPrivateProfileIntA("Combat_Net", "ItemDataPrefetch", 0, iniPath.c_str()) != 0;
        g_settings.OptMovementSmoothing  = GetPrivateProfileIntA("General", "MovementSmoothing", 0, iniPath.c_str()) != 0;
        g_settings.OptFontAlphaFastpath  = GetPrivateProfileIntA("UI_Lua", "FontAlphaFastpath", 0, iniPath.c_str()) != 0;

        // Parse Features 31-50
        g_settings.OptPacketProcessingThrottle = GetPrivateProfileIntA("Combat_Net", "PacketProcessingThrottle", 0, iniPath.c_str()) != 0;
        g_settings.OptNameplateCulling = GetPrivateProfileIntA("Combat_Net", "NameplateCulling", 0, iniPath.c_str()) != 0;
        g_settings.OptTextureUnloadDelay = GetPrivateProfileIntA("Graphics_Sound", "TextureUnloadDelay", 0, iniPath.c_str()) != 0;
        g_settings.OptM2MatrixSimd = GetPrivateProfileIntA("Graphics_Sound", "M2MatrixSimd", 0, iniPath.c_str()) != 0;
        g_settings.OptM2BoneMt = GetPrivateProfileIntA("Graphics_Sound", "M2BoneMt", 0, iniPath.c_str()) != 0;
        g_settings.OptMpqAsyncDecompress = GetPrivateProfileIntA("General", "MpqAsyncDecompress", 0, iniPath.c_str()) != 0;
        g_settings.OptSimdMatrixTransform = GetPrivateProfileIntA("Graphics_Sound", "SimdMatrixTransform", 0, iniPath.c_str()) != 0;
        g_settings.OptMinimapRefreshGovernor = GetPrivateProfileIntA("UI_Lua", "MinimapRefreshGovernor", 0, iniPath.c_str()) != 0;
        g_settings.OptSpellEffectCulling = GetPrivateProfileIntA("Graphics_Sound", "SpellEffectCulling", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaStringCompareFast = GetPrivateProfileIntA("UI_Lua", "LuaStringCompareFast", 0, iniPath.c_str()) != 0;
        g_settings.OptDbcRowCaching = GetPrivateProfileIntA("Graphics_Sound", "DbcRowCaching", 0, iniPath.c_str()) != 0;
        g_settings.OptNetworkStringDedup = GetPrivateProfileIntA("Combat_Net", "NetworkStringDedup", 0, iniPath.c_str()) != 0;
        g_settings.OptSoundFreqCoalesce = GetPrivateProfileIntA("Graphics_Sound", "SoundFreqCoalesce", 0, iniPath.c_str()) != 0;
        g_settings.OptAuraUpdateDedup = GetPrivateProfileIntA("Combat_Net", "AuraUpdateDedup", 0, iniPath.c_str()) != 0;
        g_settings.OptUiTextureCaching = GetPrivateProfileIntA("UI_Lua", "UiTextureCaching", 0, iniPath.c_str()) != 0;
        g_settings.OptWmoCullingOpt = GetPrivateProfileIntA("Graphics_Sound", "WmoCullingOpt", 0, iniPath.c_str()) != 0;
        g_settings.OptFastFloatParse = GetPrivateProfileIntA("UI_Lua", "FastFloatParse", 0, iniPath.c_str()) != 0;
        g_settings.OptHeapAllocationTracker = GetPrivateProfileIntA("General", "HeapAllocationTracker", 0, iniPath.c_str()) != 0;
        g_settings.OptCrtMimalloc = GetPrivateProfileIntA("General", "CrtMimalloc", 0, iniPath.c_str()) != 0;
        g_settings.OptSpellCooldownCache = GetPrivateProfileIntA("UI_Lua", "SpellCooldownCache", 0, iniPath.c_str()) != 0;
        g_settings.OptGuidStringCache = GetPrivateProfileIntA("Combat_Net", "GuidStringCache", 0, iniPath.c_str()) != 0;
        g_settings.OptFrameScriptMemOpt = GetPrivateProfileIntA("UI_Lua", "FrameScriptMemOpt", 0, iniPath.c_str()) != 0;
        g_settings.OptCombatEventLimit = GetPrivateProfileIntA("Combat_Net", "CombatEventLimit", 0, iniPath.c_str()) != 0;

        // Previously Init'd unconditionally - now read from their launcher keys.
        // Default 1 preserves the old always-on behavior; they are now disableable.
        g_settings.OptAddonMsgLimiter      = GetPrivateProfileIntA("Combat_Net", "AddonMsgLimiter", 0, iniPath.c_str()) != 0;
        g_settings.OptAuraPreloadCache     = GetPrivateProfileIntA("Combat_Net", "AuraPreloadCache", 0, iniPath.c_str()) != 0;
        g_settings.OptCDataStoreBuffering  = GetPrivateProfileIntA("Combat_Net", "CDataStoreBuffering", 0, iniPath.c_str()) != 0;
        g_settings.OptCameraShakeOpt       = GetPrivateProfileIntA("General", "CameraShakeOpt", 0, iniPath.c_str()) != 0;
        g_settings.OptCombatLogAsync       = GetPrivateProfileIntA("Combat_Net", "CombatLogAsync", 0, iniPath.c_str()) != 0;
        g_settings.OptCombatTextFont       = GetPrivateProfileIntA("UI_Lua", "CombatTextFont", 0, iniPath.c_str()) != 0;
        g_settings.OptDbcFileCache         = GetPrivateProfileIntA("Graphics_Sound", "DbcFileCache", 0, iniPath.c_str()) != 0;
        g_settings.OptFontOutlineCache     = GetPrivateProfileIntA("UI_Lua", "FontOutlineCache", 0, iniPath.c_str()) != 0;
        g_settings.OptMouseClipRelease     = GetPrivateProfileIntA("General", "MouseClipRelease", 0, iniPath.c_str()) != 0;
        g_settings.OptNameplateDistanceCvar= GetPrivateProfileIntA("Combat_Net", "NameplateDistanceCvar", 0, iniPath.c_str()) != 0;
        g_settings.OptParticleDensityScaler= GetPrivateProfileIntA("Graphics_Sound", "ParticleDensityScaler", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsBackup      = GetPrivateProfileIntA("General", "SavedVarsBackup", 0, iniPath.c_str()) != 0;
        g_settings.OptSoundCoalescer       = GetPrivateProfileIntA("Graphics_Sound", "SoundCoalescer", 0, iniPath.c_str()) != 0;
        g_settings.OptSpellOverlayPreload  = GetPrivateProfileIntA("Combat_Net", "SpellOverlayPreload", 0, iniPath.c_str()) != 0;
        g_settings.OptUnitMaxPowerCache    = GetPrivateProfileIntA("UI_Lua", "UnitMaxPowerCache", 0, iniPath.c_str()) != 0;

        g_loadedFrom = iniPath;
        g_settings.OptVertexBufferPrealloc = GetPrivateProfileIntA("General", "VertexBufferPrealloc", 0, iniPath.c_str()) != 0;
        g_settings.OptWorldObjectOpt       = GetPrivateProfileIntA("Graphics_Sound", "WorldObjectOpt", 0, iniPath.c_str()) != 0;
    }
}
