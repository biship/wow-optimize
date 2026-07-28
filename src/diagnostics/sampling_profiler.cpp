// ============================================================================
// Module: sampling_profiler.cpp
// Description: Samples thread contexts periodically to trace hot execution execution paths.
// Safety & Threading: Dedicated profiler thread.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "sampling_profiler.h"
#include "version.h"
#pragma comment(lib, "psapi.lib")

extern "C" void Log(const char* fmt, ...);

// Forward decl (avoid pulling in the whole lua_optimize header here). Returns
// true while a zone/UI load or transition is in progress.
namespace LuaOpt { bool IsLoadingMode(); }

namespace SamplingProfiler {

// Samples taken during loading screens / the first few seconds after start are
// not representative of steady-state play (they're dominated by MPQ/DBC load,
// hook install and page faults). Excluding them makes the profile reflect what
// actually costs frame time in the world. Counted separately for transparency.
static DWORD    g_samplerStartTick = 0;
static uint64_t g_skippedSamples = 0;
static const DWORD PROFILER_WARMUP_MS = 15000;

// ---- configuration ------------------------------------------------
static constexpr DWORD SAMPLE_INTERVAL_MS = 1;      // target interval
static constexpr int   MAX_KNOWN_FUNCS    = 256;     // address table size
static constexpr int   TOP_N              = 50;      // functions to dump
static constexpr uintptr_t WOW_BASE       = 0x00400000;
static constexpr uintptr_t WOW_END        = 0x00BFFFFF;  // wow.exe image range

// ---- known-function table -----------------------------------------
// Each entry: { address, name }. Sorted by address for binary search.
// Populated at Init() from the verified address list.
struct FuncEntry {
    uintptr_t   addr;
    const char* name;
};

struct SampleBucket {
    uintptr_t   addr;       // nearest known function (or 0 for unknown)
    const char* name;       // null if unknown
    uint64_t    count;
};

static FuncEntry    g_knownFuncs[MAX_KNOWN_FUNCS];
static int          g_knownCount = 0;

// ---- sample storage -----------------------------------------------
// We store raw EIP values and aggregate at dump time. This avoids
// lock contention during sampling. The ring buffer is written only
// by the sampler thread and read only at shutdown (after the thread
// is joined), so no synchronization is needed beyond the atomic
// write index.
static constexpr int RING_SIZE = 1 << 20;  // ~1M samples (~17 min at 1ms)
static volatile uintptr_t g_ring[RING_SIZE];
static volatile uint64_t  g_writeIdx = 0;
static volatile uint64_t  g_totalSamples = 0;

// Per-4KB-page sample counts for WoW-image samples that don't match a named
// function. Turns the opaque "unknown_wow" blob into a per-region hot-map so
// unlisted hot code is still pinpointed by address (label "wow_region_0x...").
static constexpr int NUM_PAGES = (int)((WOW_END - WOW_BASE) >> 12) + 1;  // ~2048
static uint32_t g_pageCounts[NUM_PAGES];

// ---- state --------------------------------------------------------
static HANDLE  g_mainThread  = nullptr;
static HANDLE  g_samplerThread = nullptr;
static volatile bool g_running = false;
static HMODULE g_wowModule = nullptr;

// ---- populate known-function table --------------------------------
// Add entries here as new hooks/targets are verified in disassembly.
// Keep sorted by address for binary-search lookup.
static void BuildKnownFuncTable() {
    // Format: { RVA_or_VA, "symbol_name" }
    // Using absolute VAs (base 0x400000).
    static const FuncEntry table[] = {
        // --- CRT / memory ---
        { 0x0040BB80, "memset" },
        { 0x0040CB10, "memcpy" },
        { 0x004112F8, "_msize" },
        { 0x00412FC7, "free" },
        { 0x00415074, "malloc" },
        { 0x00416A56, "calloc" },
        { 0x00416A95, "realloc" },
        { 0x00416CB0, "_recalloc" },

        // --- Math / transform library ---
        { 0x004C1B30, "CMatrix::TranslateLocal" },
        { 0x004C1F00, "CMatrix::Multiply" },
        { 0x004C2120, "CMatrix::ScalarMul" },
        { 0x004C21B0, "sub_4C21B0_pt_x_mat4" },
        { 0x004C2210, "RowAffinePoint" },
        { 0x004C2270, "sub_4C2270_vec4_x_mat4" },
        { 0x004C2300, "InPlacePointXform" },
        { 0x004C23D0, "CMatrix::Transpose" },
        { 0x004C2440, "CMatrix_AdjugateDet" },
        { 0x004C2FC0, "CMatrix::InvertRigid" },
        { 0x004C3420, "C3Vector::Normalize" },
        { 0x004C3600, "C3Vector::NormalizeGuarded" },

        // --- Object manager ---
        { 0x004D3790, "TLS_Accessor" },
        { 0x004D4DB0, "ClntObjMgrObjectPtr" },

        // --- Network / serialization ---
        { 0x00468D00, "NetPacketSend" },
        { 0x0047B340, "CDataStore_GetBytes" },
        { 0x0076DC20, "CDataStore_GetWowGUID" },

        // --- World / cleanup ---
        { 0x00528C30, "WorldExitCleanup" },
        { 0x005D9D90, "ObjMgrTeardownWalk" },

        // --- FrameScript / events ---
        { 0x0048E680, "FrameScript_Dispatch" },
        { 0x0081AC90, "FrameScript_SignalEvent" },

        // --- Lua VM core ---
        { 0x0084D9C0, "index2adr" },
        { 0x0084E030, "lua_tonumber" },
        { 0x0084E0B0, "lua_toboolean" },
        { 0x0084E1C0, "lua_touserdata" },
        { 0x0084E2A0, "lua_pushnumber" },
        { 0x0084E670, "lua_rawgeti" },
        { 0x0084E8D0, "lua_settable" },
        { 0x0084EBF0, "lua_pcall" },
        { 0x0084F9F0, "luaL_checklstring" },

        // --- Lua internals ---
        { 0x00856C80, "luaS_newlstr" },
        { 0x00856E50, "luaV_tonumber" },
        { 0x00857900, "luaV_concat" },
        { 0x0085BC10, "luaV_gettable" },
        { 0x0085C430, "luaH_getstr" },
        { 0x0085C6F0, "LuaH_resize" },
        { 0x0085CAB0, "luaH_newkey" },

        // --- Rendering / culling ---
        { 0x00821A20, "M2_DrawBatchBuilder" },
        { 0x00960D20, "Lua_Model_SetLight" },
        { 0x00979110, "CQuaternion::Normalize" },
        { 0x00981D40, "ParticleSpawn_Init" },
        { 0x00983490, "RayTriIntersect16" },
        { 0x009836B0, "RayTriIntersect32" },
        { 0x009839E0, "CFrustum::IsAABBVisible" },
        { 0x00983D70, "CFrustum::IsPointVisible" },

        // --- CRT string/memory (static) ---
        { 0x0076E5A0, "free_wrapper" },
        { 0x0076E780, "_strnicmp" },
        { 0x0076ED20, "strncpy" },
        { 0x0076EE30, "strlen" },

        // --- Combat text ---
        { 0x00608880, "CombatText_EventInit" },

        // --- Misc ---
        { 0x0081B510, "EventNameWrapper" },

        // --- Lua pattern matcher (verified this session, 0x852A10-0x853D9C) ---
        { 0x00852A10, "lua_classend" },
        { 0x00852C30, "lua_matchbalance" },
        { 0x00852F60, "lua_match" },
        { 0x00853240, "lua_lmemfind" },
        { 0x008535B0, "string.find" },
        { 0x008535D0, "string.match" },
        { 0x00853980, "string.gsub" },
        { 0x00853C50, "string.format" },

        // --- Lua string library ---
        { 0x00852400, "string.len" },
        { 0x00852430, "string.sub" },
        { 0x008524E0, "string.reverse" },
        { 0x00852580, "string.lower" },
        { 0x00852680, "string.upper" },
        { 0x00852780, "string.rep" },
        { 0x00852800, "string.byte" },
        { 0x008528D0, "string.char" },

        // --- Lua API (stack / push / query) ---
        { 0x0084DBD0, "lua_gettop" },
        { 0x0084DBF0, "lua_settop" },
        { 0x0084DC50, "lua_remove" },
        { 0x0084DCC0, "lua_insert" },
        { 0x0084DEB0, "lua_type" },
        { 0x0084DF20, "lua_isnumber" },
        { 0x0084E280, "lua_pushnil" },
        { 0x0084E2D0, "lua_pushinteger" },
        { 0x0084E350, "lua_pushstring" },
        { 0x0084E590, "lua_getfield" },
        { 0x0084E900, "lua_setfield" },
        { 0x0084E970, "lua_rawset" },
        { 0x0084EC30, "lua_call" },
        { 0x0084ED50, "lua_gc" },

        // --- Lua VM dispatch / tables ---
        { 0x00856760, "luaD_call" },
        { 0x00857250, "luaV_gettable" },
        { 0x008573C0, "luaV_settable" },

        // --- Lua base / conversion / table lib ---
        { 0x00851C30, "table.concat" },
        { 0x00854100, "tonumber" },
        { 0x00854660, "type" },
        { 0x00854A20, "tostring" },
    };

    g_knownCount = 0;
    for (const auto& e : table) {
        if (g_knownCount >= MAX_KNOWN_FUNCS) break;
        g_knownFuncs[g_knownCount++] = e;
    }

    // Sort by address for binary search
    std::sort(g_knownFuncs, g_knownFuncs + g_knownCount,
              [](const FuncEntry& a, const FuncEntry& b) { return a.addr < b.addr; });
}

// Find the nearest known function at or below the given address.
// Returns nullptr if no known function is within 4KB (likely not in a
// function we care about, or in an unlisted helper).
static const FuncEntry* FindNearestFunc(uintptr_t eip) {
    if (g_knownCount == 0) return nullptr;

    // Binary search for the largest addr <= eip
    int lo = 0, hi = g_knownCount - 1;
    int best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_knownFuncs[mid].addr <= eip) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (best < 0) return nullptr;

    // Only match if within 4KB of the function start (reasonable
    // upper bound for a single WoW function body).
    uintptr_t delta = eip - g_knownFuncs[best].addr;
    if (delta > 4096) return nullptr;

    return &g_knownFuncs[best];
}

// ---- sampler thread -----------------------------------------------
static DWORD WINAPI SamplerThreadProc(LPVOID) {
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_CONTROL;  // just EIP + segment regs

    if (g_samplerStartTick == 0) g_samplerStartTick = GetTickCount();

    while (g_running) {
        // Suspend → read → resume. The window is ~microseconds;
        // WoW won't notice. Same technique crash dumpers use.
        if (SuspendThread(g_mainThread) != (DWORD)-1) {
            uintptr_t eip = 0;
            if (GetThreadContext(g_mainThread, &ctx)) eip = (uintptr_t)ctx.Eip;
            ResumeThread(g_mainThread);  // resume ASAP, then decide off-thread

            if (eip) {
                // Only record steady-state, in-world samples: skip the warmup
                // window and any loading/transition. This keeps startup one-shots
                // (hook install, MPQ/DBC load) and load-screen page-fault spikes
                // out of the "what costs frame time in play" picture.
                bool warmedUp = (GetTickCount() - g_samplerStartTick) >= PROFILER_WARMUP_MS;
                if (warmedUp && !LuaOpt::IsLoadingMode()) {
                    uint64_t idx = g_writeIdx % RING_SIZE;
                    g_ring[idx] = eip;
                    g_writeIdx++;
                    g_totalSamples++;
                } else {
                    g_skippedSamples++;
                }
            }
        }

        Sleep(SAMPLE_INTERVAL_MS);
    }

    return 0;
}

// ---- system-module classification ---------------------------------
// Samples that land outside the WoW image are otherwise lumped into one
// opaque "system_dll" bucket. On DXVK that bucket can be the majority of
// main-thread time, and it matters a great deal WHICH module it is:
// d3d9.dll/vulkan-1.dll = GPU present/sync wait (not CPU-fixable), while
// ntdll.dll = page-fault / heap work (fixable by reducing memory pressure).
// Enumerate loaded modules once per dump and range-classify each system sample.
struct ModRange { uintptr_t base; uintptr_t end; char name[32]; uint64_t count; };
static ModRange g_mods[128];
static int g_modCount = 0;

// Our own DLL is broken down per-4KB-page too (like the WoW image), because it
// showed up as a top-4 consumer (~8% of main-thread time) and we need to know
// WHICH of our hooks costs that. Reported as "wowopt+0xNNNN" (offset from our
// DLL base) so it maps directly to wow_optimize.map.
static uintptr_t g_selfBase = 0;
static uintptr_t g_selfEnd  = 0;

// Our own functions, by absolute address, so a hot spot inside this DLL prints a
// name instead of an offset nobody can resolve without the matching .map.
static constexpr int MAX_SELF_SYMBOLS = 64;
struct SelfSymbol { uintptr_t addr; const char* name; };
static SelfSymbol g_selfSymbols[MAX_SELF_SYMBOLS] = {};
static int        g_selfSymbolCount = 0;

void RegisterSelfSymbol(const char* name, const void* addr) {
    if (!name || !addr || g_selfSymbolCount >= MAX_SELF_SYMBOLS) return;
    g_selfSymbols[g_selfSymbolCount].addr = (uintptr_t)addr;
    g_selfSymbols[g_selfSymbolCount].name = name;
    g_selfSymbolCount++;
}

// Nearest registered symbol at or below addr, within a sane distance. The bound
// matters: without it every unregistered hot spot would be attributed to whichever
// registered function happens to sit lowest in the image, which is worse than
// admitting we do not know.
static const char* ResolveSelfSymbol(uintptr_t addr) {
    const char* best = nullptr;
    uintptr_t bestDelta = 0x4000;   // 16 KB
    for (int i = 0; i < g_selfSymbolCount; i++) {
        if (g_selfSymbols[i].addr > addr) continue;
        uintptr_t d = addr - g_selfSymbols[i].addr;
        if (d < bestDelta) {
            bestDelta = d;
            best = g_selfSymbols[i].name;
        }
    }
    return best;
}
static constexpr int SELF_PAGES = 4096;   // covers a 16MB image
static uint32_t g_selfPageCounts[SELF_PAGES];

// The 4KB page above ranks our DLL against everything else, but it cannot answer
// WHICH hook is hot: at this build's code density a single page holds about
// nineteen functions, so a page that reads 1.97% could be one expensive hook or
// nineteen cheap ones. Resolving that from a tester log used to mean rebuilding
// the exact commit just to read its .map, and even then a page listed too many
// candidates to choose between.
//
// So our own image is counted a second time at 256-byte resolution, and reported
// as its own section rather than merged into the main ranking - splitting our
// share across eight buckets would push every one of them below the top-N cutoff
// and hide the very thing this is for.
static constexpr int SELF_FINE_SHIFT = 8;      // 256-byte buckets
static constexpr int SELF_FINE_SLOTS = 8192;   // covers a 2MB image (~600KB today)
static constexpr int SELF_FINE_TOP   = 20;
static uint32_t g_selfFineCounts[SELF_FINE_SLOTS];

// The client's code has exactly the same problem, and it is the bigger half: the
// hottest region in a recent profile, 0x005B2000 at 6.45%, holds four functions,
// so "which client function is worth hooking" could not be answered from a log at
// all. 512 bytes over the 8MB image costs 64KB of counters and usually lands on
// one function, which can then be decompiled directly.
static constexpr int WOW_FINE_SHIFT = 9;       // 512-byte buckets
static constexpr int WOW_FINE_SLOTS = (int)((WOW_END - WOW_BASE) >> WOW_FINE_SHIFT) + 1;
static uint32_t g_wowFineCounts[WOW_FINE_SLOTS];

// Prints the top SELF_FINE_TOP buckets of a histogram, largest first. Selection is
// an insertion pass over a 20-entry list rather than a sort of the whole array,
// which would mean copying 16-32K entries inside a diagnostic dump.
static bool IsWaitSymbol(const char* n) {
    if (!n) return false;
    if (n[0] != 'N' || n[1] != 't') return false;
    return strncmp(n, "NtWaitFor", 9) == 0 ||
           strncmp(n, "NtDelayExecution", 16) == 0 ||
           strncmp(n, "NtRemoveIoCompletion", 20) == 0;
}

static void DumpFineHistogram(const uint32_t* counts, int slots, int shift,
                              uint64_t total, const char* title,
                              const char* addrFormat, uintptr_t addrBase) {
    int idx[SELF_FINE_TOP];
    int found = 0;

    for (int i = 0; i < slots; i++) {
        uint32_t c = counts[i];
        if (!c) continue;
        int at = found;
        if (found < SELF_FINE_TOP) {
            found++;
        } else if (c > counts[idx[SELF_FINE_TOP - 1]]) {
            at = SELF_FINE_TOP - 1;
        } else {
            continue;
        }
        while (at > 0 && c > counts[idx[at - 1]]) {
            idx[at] = idx[at - 1];
            at--;
        }
        idx[at] = i;
    }

    if (found == 0 || total == 0) return;

    Log("[SamplingProfiler] === %s ===", title);
    for (int i = 0; i < found; i++) {
        uint32_t c = counts[idx[i]];
        char addr[32];
        uintptr_t slotAddr = addrBase + ((uintptr_t)idx[i] << shift);
        const char* sym = (addrBase == 0) ? ResolveSelfSymbol(g_selfBase + slotAddr) : nullptr;
        if (sym) wsprintfA(addr, "wowopt!%.20s", sym);
        else     wsprintfA(addr, addrFormat, (unsigned)slotAddr);
        Log("[SamplingProfiler]   %-14s %8u samples (%5.2f%%)",
            addr, c, 100.0 * (double)c / (double)total);
    }
}

// A cheap marker: this variable lives inside our own DLL, so its address tells
// us which module is ours.
static int g_selfAnchor = 0;

static void BuildModuleTable() {
    g_modCount = 0;
    g_selfBase = g_selfEnd = 0;
    HMODULE mods[256];
    DWORD needed = 0;
    HANDLE proc = GetCurrentProcess();
    if (!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return;
    int count = (int)(needed / sizeof(HMODULE));
    if (count > 256) count = 256;
    for (int i = 0; i < count && g_modCount < 128; i++) {
        MODULEINFO mi;
        if (!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
        uintptr_t base = (uintptr_t)mi.lpBaseOfDll;
        uintptr_t end  = base + mi.SizeOfImage;
        // Skip the wow.exe main image — those samples are already handled by
        // the named-function / per-page buckets (WOW_BASE..WOW_END).
        if (base <= WOW_BASE && WOW_BASE < end) continue;
        // Identify our own DLL by the anchor address; it gets a per-page
        // breakdown instead of a single module bucket.
        if ((uintptr_t)&g_selfAnchor >= base && (uintptr_t)&g_selfAnchor < end) {
            g_selfBase = base;
            g_selfEnd  = end;
            continue;
        }
        char nm[MAX_PATH];
        if (!GetModuleBaseNameA(proc, mods[i], nm, sizeof(nm))) continue;
        ModRange& m = g_mods[g_modCount];
        m.base = base;
        m.end  = end;
        strncpy(m.name, nm, sizeof(m.name) - 1);
        m.name[sizeof(m.name) - 1] = '\0';
        m.count = 0;
        g_modCount++;
    }
}

static ModRange* FindModule(uintptr_t eip) {
    for (int i = 0; i < g_modCount; i++) {
        if (eip >= g_mods[i].base && eip < g_mods[i].end) return &g_mods[i];
    }
    return nullptr;
}

// ntdll shows up as one big bucket (~40%+ of main-thread time), but that lumps
// together two very different things: threads BLOCKED in a wait (NtWait* /
// NtDelayExecution = idle GPU/frame-pacing, NOT CPU we can cut) versus HEAP work
// (RtlAllocateHeap/RtlFreeHeap = reducible by cutting allocations). Resolving
// ntdll samples to the nearest key exported function tells us which - i.e.
// whether a memory optimization is even worth attempting.
struct NtFunc { uintptr_t addr; const char* name; uint64_t count; };
static NtFunc g_ntFuncs[24];
static int g_ntFuncCount = 0;
static uintptr_t g_ntdllBase = 0, g_ntdllEnd = 0;

static void BuildNtFuncTable() {
    g_ntFuncCount = 0; g_ntdllBase = g_ntdllEnd = 0;
    HMODULE h = GetModuleHandleA("ntdll.dll");
    if (!h) return;
    MODULEINFO mi;
    if (GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) {
        g_ntdllBase = (uintptr_t)mi.lpBaseOfDll;
        g_ntdllEnd  = g_ntdllBase + mi.SizeOfImage;
    }
    static const char* const names[] = {
        // blocked in a wait -> idle (GPU/frame-pacing/lock), not fixable CPU work
        "NtWaitForSingleObject", "NtWaitForMultipleObjects", "NtDelayExecution",
        "NtWaitForAlertByThreadId", "NtSignalAndWaitForSingleObject", "NtRemoveIoCompletion",
        // heap -> real CPU work, reducible by cutting main-thread allocations
        "RtlAllocateHeap", "RtlFreeHeap", "RtlReAllocateHeap", "RtlSizeHeap",
        // locks / dispatch
        "RtlEnterCriticalSection", "RtlLeaveCriticalSection",
        "KiUserCallbackDispatcher", "KiUserApcDispatcher", "KiUserExceptionDispatcher",
    };
    for (int i = 0; i < (int)(sizeof(names)/sizeof(names[0])) && g_ntFuncCount < 24; i++) {
        void* p = (void*)GetProcAddress(h, names[i]);
        if (p) {
            g_ntFuncs[g_ntFuncCount].addr = (uintptr_t)p;
            g_ntFuncs[g_ntFuncCount].name = names[i];
            g_ntFuncs[g_ntFuncCount].count = 0;
            g_ntFuncCount++;
        }
    }
    for (int i = 1; i < g_ntFuncCount; i++) {  // insertion sort by address
        NtFunc t = g_ntFuncs[i]; int j = i - 1;
        while (j >= 0 && g_ntFuncs[j].addr > t.addr) { g_ntFuncs[j+1] = g_ntFuncs[j]; j--; }
        g_ntFuncs[j+1] = t;
    }
}

// Nearest key ntdll function at or below eip, within 64KB. Wait stubs are leaf
// syscalls so a blocked thread lands right on them (precise); heap internals are
// approximate but a heap-heavy cluster is still unmistakable.
static NtFunc* FindNtFunc(uintptr_t eip) {
    NtFunc* best = nullptr;
    for (int i = 0; i < g_ntFuncCount; i++) {
        if (g_ntFuncs[i].addr <= eip) best = &g_ntFuncs[i];
        else break;
    }
    if (best && (eip - best->addr) <= 0x10000) return best;
    return nullptr;
}

// ---- aggregation + dump -------------------------------------------
static void DumpResults() {
    uint64_t total = g_totalSamples;
    if (total == 0) {
        Log("[SamplingProfiler] No samples collected");
        return;
    }

    // Reset the per-page tally so DumpResults can be called repeatedly (the
    // periodic stats dump calls it every few minutes, not only at shutdown —
    // the shutdown path is often skipped on the fast process-exit, which lost
    // the profile entirely). Each call re-aggregates the current ring contents.
    memset(g_pageCounts, 0, sizeof(g_pageCounts));
    memset(g_selfPageCounts, 0, sizeof(g_selfPageCounts));
    memset(g_selfFineCounts, 0, sizeof(g_selfFineCounts));
    memset(g_wowFineCounts, 0, sizeof(g_wowFineCounts));

    // Snapshot loaded modules so system samples can be attributed to a DLL.
    BuildModuleTable();
    BuildNtFuncTable();

    // Buckets: one per named function, one "system_dll", plus one per non-empty
    // 4KB WoW page (so unlisted hot code is reported by address, not lumped into
    // a single opaque blob). Static (not on the stack) because of the page slots.
    static constexpr int MAX_BUCKETS = MAX_KNOWN_FUNCS + NUM_PAGES + SELF_PAGES + 128 + 24 + 1;
    static SampleBucket buckets[MAX_BUCKETS];
    int bucketCount = 0;

    // Initialize buckets from known funcs
    for (int i = 0; i < g_knownCount; i++) {
        buckets[bucketCount].addr  = g_knownFuncs[i].addr;
        buckets[bucketCount].name  = g_knownFuncs[i].name;
        buckets[bucketCount].count = 0;
        bucketCount++;
    }

    // System (outside the WoW image) bucket
    int systemIdx = bucketCount;
    buckets[bucketCount].addr  = 0;
    buckets[bucketCount].name  = "system_dll";
    buckets[bucketCount].count = 0;
    bucketCount++;

    // Walk the ring buffer and bucket each sample
    uint64_t n = (total < RING_SIZE) ? total : RING_SIZE;
    uint64_t startIdx = (total <= RING_SIZE) ? 0 : (total - RING_SIZE);

    for (uint64_t i = 0; i < n; i++) {
        uintptr_t eip = g_ring[(startIdx + i) % RING_SIZE];

        const FuncEntry* f = FindNearestFunc(eip);
        if (f) {
            // Find the bucket for this function (linear scan — only at
            // dump time, not on the hot path).
            for (int b = 0; b < g_knownCount; b++) {
                if (buckets[b].addr == f->addr) {
                    buckets[b].count++;
                    goto next_sample;
                }
            }
        }

        // Not matched to a named function: aggregate WoW samples per 4KB page,
        // our own DLL per 4KB page, and other non-WoW samples per owning module
        // (falling back to the opaque system bucket only when unresolved).
        if (eip >= WOW_BASE && eip <= WOW_END) {
            uintptr_t woff = eip - WOW_BASE;
            g_pageCounts[woff >> 12]++;
            uint32_t wfine = (uint32_t)(woff >> WOW_FINE_SHIFT);
            if (wfine < WOW_FINE_SLOTS) g_wowFineCounts[wfine]++;
        } else if (g_selfBase && eip >= g_selfBase && eip < g_selfEnd) {
            uintptr_t off = eip - g_selfBase;
            uint32_t pg = (uint32_t)(off >> 12);
            if (pg < SELF_PAGES) g_selfPageCounts[pg]++;
            uint32_t fine = (uint32_t)(off >> SELF_FINE_SHIFT);
            if (fine < SELF_FINE_SLOTS) g_selfFineCounts[fine]++;
        } else {
            ModRange* m = FindModule(eip);
            if (m) {
                if (g_ntdllBase && eip >= g_ntdllBase && eip < g_ntdllEnd) {
                    NtFunc* nf = FindNtFunc(eip);
                    if (nf) nf->count++;   // attributed to a known ntdll function
                    else    m->count++;    // ntdll but unrecognized -> stays in ntdll bucket
                } else {
                    m->count++;
                }
            } else {
                buckets[systemIdx].count++;
            }
        }
        next_sample:;
    }

    // Emit one bucket per non-empty page of our own DLL (labelled "wowopt+0x..").
    for (int p = 0; p < SELF_PAGES && bucketCount < MAX_BUCKETS; p++) {
        if (!g_selfPageCounts[p]) continue;
        buckets[bucketCount].addr  = g_selfBase + ((uintptr_t)p << 12);
        buckets[bucketCount].name  = nullptr;   // labelled by self-offset at print time
        buckets[bucketCount].count = g_selfPageCounts[p];
        bucketCount++;
    }

    // Emit one bucket per system module that got samples (labelled "sys:<dll>").
    for (int mi = 0; mi < g_modCount && bucketCount < MAX_BUCKETS; mi++) {
        if (!g_mods[mi].count) continue;
        buckets[bucketCount].addr  = g_mods[mi].base;
        buckets[bucketCount].name  = g_mods[mi].name;  // e.g. "d3d9.dll", "ntdll.dll"
        buckets[bucketCount].count = g_mods[mi].count;
        bucketCount++;
    }

    // Emit ntdll sub-function buckets (e.g. "ntdll!NtWaitForSingleObject") so the
    // big ntdll bucket is split into idle-wait vs heap vs locks.
    for (int i = 0; i < g_ntFuncCount && bucketCount < MAX_BUCKETS; i++) {
        if (!g_ntFuncs[i].count) continue;
        buckets[bucketCount].addr  = g_ntFuncs[i].addr;
        buckets[bucketCount].name  = g_ntFuncs[i].name;  // e.g. "NtWaitForSingleObject"
        buckets[bucketCount].count = g_ntFuncs[i].count;
        bucketCount++;
    }

    // Merge every non-empty page region as an address-labelled bucket.
    for (int p = 0; p < NUM_PAGES && bucketCount < MAX_BUCKETS; p++) {
        if (!g_pageCounts[p]) continue;
        buckets[bucketCount].addr  = WOW_BASE + ((uintptr_t)p << 12);
        buckets[bucketCount].name  = nullptr;   // labelled by address at print time
        buckets[bucketCount].count = g_pageCounts[p];
        bucketCount++;
    }

    // Sort by count descending
    std::sort(buckets, buckets + bucketCount,
              [](const SampleBucket& a, const SampleBucket& b) {
                  return a.count > b.count;
              });

    // A blocked thread is not a hot function, and must never be ranked as one.
    // The first version of this report excluded waits from the denominator but
    // still listed them, so NtDelayExecution appeared in a table of hot functions
    // holding "17.42% of executing" - a share of exactly the thing it was not
    // doing.
    // Separate "the thread was blocked" from "the thread was running code".
    //
    // Every sample landing in one of these is the main thread parked in the
    // kernel with nothing to do - waiting on the present queue, a vsync interval,
    // an event or an IO completion. Listing them by heat put
    // NtWaitForAlertByThreadId at the top of a table headed HOT FUNCTIONS, where
    // it reads as the most expensive thing in the client rather than as proof
    // that the client had no work at all. A tester session came back with 94.8%
    // of samples there: 0.9ms of CPU per 16.7ms frame, with every optimization in
    // this DLL competing for the remaining 5%.
    //
    // Knowing which side of that line a session falls on decides whether any CPU
    // work here can matter, so it is now the first thing the profile says.
    uint64_t waitSamples = 0;
    for (int i = 0; i < bucketCount; i++) {
        if (IsWaitSymbol(buckets[i].name)) waitSamples += buckets[i].count;
    }
    uint64_t workSamples = (total > waitSamples) ? (total - waitSamples) : 0;
    double   workPct     = total ? (100.0 * (double)workSamples / (double)total) : 0.0;

    Log("[SamplingProfiler] === MAIN THREAD: %.1f%% executing, %.1f%% blocked "
        "(%llu of %llu steady-state samples were a kernel wait) ===",
        workPct, 100.0 - workPct,
        (unsigned long long)waitSamples, (unsigned long long)total);
    if (total >= 1000) {
        if (workPct < 15.0) {
            Log("[SamplingProfiler]   The client is not CPU-bound here - it spends "
                "the frame waiting on the GPU, vsync or a frame limiter. CPU-side "
                "optimizations cannot show up in this session no matter how good "
                "they are; uncap the frame rate or profile a heavier scene to get "
                "a workload where they can.");
        } else if (workPct > 60.0) {
            Log("[SamplingProfiler]   The client IS CPU-bound here. The list below "
                "is where the frame time actually goes.");
        }
    }

    // Dump top-N (named functions and hot unlisted regions intermixed by heat).
    // Two percentages: of everything, and of the time the thread was running -
    // the second is the one that says how much of a real optimization target
    // something is, and it is the one that was missing.
    Log("[SamplingProfiler] === TOP %d HOT FUNCTIONS/REGIONS (%llu steady-state samples, %llu skipped: loading/warmup) ===",
        TOP_N, (unsigned long long)total, (unsigned long long)g_skippedSamples);

    int printed = 0;
    for (int i = 0; i < bucketCount && printed < TOP_N; i++) {
        if (buckets[i].count == 0) break;
        double pct = 100.0 * (double)buckets[i].count / (double)total;
        char label[40];
        const char* name;
        if (buckets[i].name) {
            name = buckets[i].name;
        } else if (g_selfBase && buckets[i].addr >= g_selfBase && buckets[i].addr < g_selfEnd) {
            // A hot page inside our own DLL — label by offset from our base so it
            // maps directly to wow_optimize.map (which of our hooks costs time).
            const char* sym = ResolveSelfSymbol(buckets[i].addr);
            if (sym) wsprintfA(label, "wowopt!%.24s", sym);
            else     wsprintfA(label, "wowopt+0x%05X", (unsigned)(buckets[i].addr - g_selfBase));
            name = label;
        } else {
            // Unlisted WoW code region — label by its 4KB page base so the
            // region can be decompiled directly from the dump.
            wsprintfA(label, "wow_region_0x%08X", (unsigned)buckets[i].addr);
            name = label;
        }
        if (IsWaitSymbol(buckets[i].name)) {
            Log("[SamplingProfiler] %3d. %-24s  %8llu samples (%5.2f%% total, blocked - not executing)",
                printed + 1, name, (unsigned long long)buckets[i].count, pct);
        } else {
            double workPctOfEntry = workSamples
                ? (100.0 * (double)buckets[i].count / (double)workSamples) : 0.0;
            Log("[SamplingProfiler] %3d. %-24s  %8llu samples (%5.2f%% total, %5.2f%% of executing)",
                printed + 1, name, (unsigned long long)buckets[i].count, pct, workPctOfEntry);
        }
        printed++;
    }

    // Our own hot spots at 256-byte resolution, then the client's at 512-byte.
    // Both are narrow enough to land on a single function, which the 4KB page
    // buckets in the ranking above cannot do.
    DumpFineHistogram(g_selfFineCounts, SELF_FINE_SLOTS, SELF_FINE_SHIFT, total,
                      "wow_optimize.dll HOT SPOTS (256-byte resolution)", "wowopt+0x%05X", 0);
    DumpFineHistogram(g_wowFineCounts, WOW_FINE_SLOTS, WOW_FINE_SHIFT, total,
                      "wow.exe HOT SPOTS (512-byte resolution)", "0x%08X", WOW_BASE);

    Log("[SamplingProfiler] === END PROFILE ===");
}

// ---- public API ---------------------------------------------------
bool Init(HANDLE mainThread) {
    if (!mainThread) return false;

    // Duplicate so this module owns an independent, long-lived handle —
    // the caller only needs the thread open for the duration of this call
    // and closes its own copy right after Init() returns, but the sampler
    // thread below uses g_mainThread for the rest of the process lifetime.
    if (!DuplicateHandle(GetCurrentProcess(), mainThread, GetCurrentProcess(),
                          &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        Log("[SamplingProfiler] FAILED to duplicate main thread handle (err=%u)", GetLastError());
        return false;
    }
    g_writeIdx = 0;
    g_totalSamples = 0;
    g_skippedSamples = 0;
    g_samplerStartTick = 0;  // re-arm the warmup window on (re)start
    memset((void*)g_ring, 0, sizeof(g_ring));
    memset(g_pageCounts, 0, sizeof(g_pageCounts));

    BuildKnownFuncTable();

    g_running = true;
    g_samplerThread = CreateThread(
        nullptr,
        64 * 1024,           // small stack — we barely use any
        SamplerThreadProc,
        nullptr,
        0,                   // run immediately
        nullptr
    );

    if (!g_samplerThread) {
        Log("[SamplingProfiler] FAILED to create sampler thread (err=%u)", GetLastError());
        g_running = false;
        CloseHandle(g_mainThread);   // release the handle duplicated above
        g_mainThread = nullptr;
        return false;
    }

    // Lowest priority — never compete with WoW's threads
    SetThreadPriority(g_samplerThread, THREAD_PRIORITY_IDLE);

    Log("[SamplingProfiler] INIT (interval=%dms, known_funcs=%d, ring=%d entries)",
        SAMPLE_INTERVAL_MS, g_knownCount, RING_SIZE);
    return true;
}

void Shutdown() {
    if (!g_running) return;

    g_running = false;

    // Wait for the sampler thread to exit (it checks g_running each iteration)
    if (g_samplerThread) {
        WaitForSingleObject(g_samplerThread, 2000);
        CloseHandle(g_samplerThread);
        g_samplerThread = nullptr;
    }

    DumpResults();

    if (g_mainThread) {
        CloseHandle(g_mainThread);
        g_mainThread = nullptr;
    }

    Log("[SamplingProfiler] SHUTDOWN (total_samples=%llu)",
        (unsigned long long)g_totalSamples);
}

bool IsActive() { return g_running; }

uint64_t GetSampleCount() { return g_totalSamples; }

// Dump the current top-50 without stopping the sampler. Safe to call from the
// main thread while sampling continues (it only reads the ring). Used by the
// periodic stats dump so the profile survives the fast process-exit path that
// skips Shutdown().
void DumpNow() {
    if (!g_running) return;
    DumpResults();
}

} // namespace SamplingProfiler