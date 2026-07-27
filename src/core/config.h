#pragma once
#ifndef WOW_OPT_CONFIG_H
#define WOW_OPT_CONFIG_H

namespace Config {
    struct Settings {
        // General & Memory
        bool OptSleepPrecision = true;
        int SleepPrecisionValue = 8;
        // One log per session preserves earlier runs, which is the whole point
        // when a tester is comparing two configurations - a single overwritten
        // file keeps only the last one. Turning this off restores that older
        // behaviour for anyone who would rather not accumulate files.
        bool OptSessionLogs = true;
        // Reimplements 16 core Lua stack API functions, including the three that
        // shift values around (lua_remove, lua_insert, lua_replace). Off by
        // default: an error of one slot there moves every later argument, and the
        // logs of two testers running it are full of exactly that shape of failure.
        bool OptLuaStackFast = false;
        // How many session logs to keep. The oldest beyond this are deleted at
        // startup, so the folder stops growing without anyone having to tidy it.
        int SessionLogsToKeep = 10;
        bool OptMemoryPressure = true;
        bool OptHeapCompactor = true;
        bool OptDefragLf = false;
        bool OptVulkanDXVK = false;
        bool OptTimingFix = false;
        bool OptCvarNullGuard = true; // Safe default: enabled
        bool OptFrameLimiter = false;
        bool OptMpqMmapVfs = false;
        bool OptMpqPrefetch = false;
        bool OptObjVisCache = false;
        bool OptDbcPreload = false;
        bool OptOomGovernor = false;
        bool OptHardwareCursor = false;
        bool OptSamplingProfiler = false;
        bool OptMimallocLarge = false;
        bool OptVaArena = false;   // EXPERIMENTAL opt-in: segregated VirtualAlloc arena (anti-fragmentation)
        bool OptCompatMode = false; // Compatibility: skip aggressive CPU-priority/affinity/working-set tweaks (for VMs/HyperV where they break the connection)

        // UI & Lua
        bool OptUIFrameBatch = false;
        bool OptAddonDispatcher = false;
        bool OptUIFrameAccessorFast = false;
        bool OptFontMetricsFast = false;
        bool OptModuleHandleCache = false;
        bool OptFrameScriptDispatch = false;
        bool OptLuaNumConvFast = false;
        bool OptLuaOpcache = false;
        bool OptLuaGcCoalesce = false;
        bool OptLuaJIT = false;
        bool OptLuaGetTimeFast = false;
        bool OptSimdMatrixTransform = false;
        bool OptAsyncTexLoader = false;
        bool OptAsyncTerrainLoader = false;
        bool OptRcuObjMgr = false;
        bool OptMipBiasGovernor = false;
        
        // Combat & Network
        bool OptCombatLogLeakFix = true; // Proven fix: extend combat log retention 300s->1800s (writes the retention CVar). Default on.
        bool OptCombatLogParser = false;
        bool OptCombatLogIncremental = false;
        bool OptEventCoalescer = false;
        bool OptSavedVarsSerializer = false;
        bool OptSavedVarsAsync = false;
        bool OptSavedVarsPretoken = false;
        bool OptUnitAuraFast = false;
        bool OptNetworkGuidSse2 = false;
        bool OptGetSpellInfoCache = false;
        bool OptPacketOffload = false;
        bool OptNameplateMT = false;

        // Graphics & Sound
        // Hooks luaS_newlstr, through which every Lua string in the game is
        // interned. Default off: it duplicates work the engine already does
        // (hash, bucket walk, compare) and falls through to the original on a
        // miss, so a miss costs both - and its value has never been measured.
        // Both of these had a launcher switch whose ini key nothing read, so the
        // hooks installed regardless and a tester bisecting string corruption got
        // no signal from turning them off.
        bool OptFastMemsetOpt = true;    // SSE2 memset replacement (0x0040BB80)
        bool OptFastStrnicmpOpt = true;  // SSE2 _strnicmp replacement (0x0076E780)
        bool OptLuaSNewLstrFast = false;
        bool OptStrStrSse2 = false;
        bool OptStrCatFast = false;
        bool OptSoundMixerOpt = false;
        bool OptAudioDecodeMt = false;
        bool OptDbcLookupCache = false;
        bool OptWorldStateCoalesce = false;
        bool OptD3d9RenderThread = false;

        // 10 new features
        bool OptCombatLogFilter = true;
        bool OptSoundVolumeLimit = true;
        bool OptUILayoutThrottle = true;
        bool OptTerrainHeightCache = true;
        bool OptAnimBlendCache = true;
        bool OptSavedVarsOpt = true;
        bool OptItemDataPrefetch = true;
        bool OptMovementSmoothing = true;
        bool OptFontAlphaFastpath = true;

        bool OptPacketProcessingThrottle = true;
        bool OptNameplateCulling = true;
        bool OptTextureUnloadDelay = false;
        bool OptM2MatrixSimd = true;
        bool OptM2BoneMt = false;
        bool OptMpqAsyncDecompress = false;
        bool OptMinimapRefreshGovernor = true;
        bool OptSpellEffectCulling = true;
        bool OptLuaStringCompareFast = true;
        bool OptDbcRowCaching = true;
        bool OptNetworkStringDedup = true;
        bool OptSoundFreqCoalesce = true;
        bool OptAuraUpdateDedup = true;
        bool OptUiTextureCaching = true;
        bool OptWmoCullingOpt = true;
        bool OptFastFloatParse = true;
        bool OptHeapAllocationTracker = true;
        bool OptCrtMimalloc = false;
        bool OptSpellCooldownCache = true;
        bool OptGuidStringCache = true;
        bool OptFrameScriptMemOpt = true;
        bool OptCombatEventLimit = true;

        // Previously Init'd unconditionally (ignored their launcher toggles).
        // Default true = preserve the old always-on behavior; now disableable.
        bool OptAddonMsgLimiter = true;
        bool OptAuraPreloadCache = true;
        bool OptCDataStoreBuffering = true;
        bool OptCameraShakeOpt = true;
        bool OptCombatLogAsync = true;
        bool OptCombatTextFont = true;
        bool OptDbcFileCache = true;
        bool OptFontOutlineCache = true;
        bool OptMouseClipRelease = true;
        bool OptNameplateDistanceCvar = true;
        bool OptParticleDensityScaler = true;
        bool OptSavedVarsBackup = true;
        bool OptSoundCoalescer = true;
        bool OptSpellOverlayPreload = true;
        bool OptUnitMaxPowerCache = true;
        bool OptVertexBufferPrealloc = true;
        bool OptWorldObjectOpt = true;
    };

    extern Settings g_settings;

    // Load settings from wow_opt.ini
    void Load();

    // Writes the resolved ini path and its contents to the log. Call after Load
    // and after logging is up, so every report says what was actually enabled.
    void DumpToLog();
}

#endif // WOW_OPT_CONFIG_H
