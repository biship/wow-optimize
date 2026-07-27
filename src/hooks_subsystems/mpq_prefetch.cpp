// ============================================================================
// Module: mpq_prefetch.cpp
// Description: Predicts zone changes and pre-caches zone WMO/ADT/WDT files
//              on background threads using private Storm archive handles.
// Safety & Threading: Thread-safe queue and worker threads.
// ============================================================================

#include "mpq_prefetch.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include "win_mutex.h"
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

extern "C" void Log(const char* fmt, ...);

namespace MPQPrefetch {

// Storm API Types
typedef BOOL (APIENTRY *SFileOpenArchive_fn)(const char* szArchiveName, DWORD dwPriority, DWORD dwFlags, HANDLE* phArchive);
typedef BOOL (APIENTRY *SFileOpenFileEx_fn)(HANDLE hArchive, const char* szFileName, DWORD dwSearchScope, HANDLE* phFile);
typedef BOOL (APIENTRY *SFileReadFile_fn)(HANDLE hFile, void* lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
typedef BOOL (APIENTRY *SFileCloseFile_fn)(HANDLE hFile);
typedef BOOL (APIENTRY *SFileCloseArchive_fn)(HANDLE hArchive);

static SFileOpenArchive_fn pSFileOpenArchive = nullptr;
static SFileOpenFileEx_fn pSFileOpenFileEx = nullptr;
static SFileReadFile_fn pSFileReadFile = nullptr;
static SFileCloseFile_fn pSFileCloseFile = nullptr;
static SFileCloseArchive_fn pSFileCloseArchive = nullptr;

static std::vector<HANDLE> g_privateArchives;

// ================================================================
// Zone Transition Tracking
// ================================================================
struct ZoneTransition {
    int fromZone;
    int toZone;
    DWORD timestamp;  // GetTickCount
};

static constexpr int ZONE_HISTORY_SIZE = 10;
static ZoneTransition g_zoneHistory[ZONE_HISTORY_SIZE] = {};
static int g_zoneHistoryHead = 0;
static int g_currentZone = 0;
static SRWLOCK g_zoneLock = SRWLOCK_INIT;

// ================================================================
// Zone → File Mapping (common files per zone)
// ================================================================
static std::unordered_map<int, std::vector<std::string>> g_zoneFileMap;

// ================================================================
// Prefetch Request Structure
// ================================================================
struct PrefetchRequest {
    char filename[260];   // File to prefetch
    int priority;         // Priority (0 = normal, 1 = high)
    int predictedZone;    // Zone this file is for
};

// ================================================================
// Thread-Safe Queue & Worker Variables
// ================================================================
static std::vector<HANDLE> g_workerThreads;
static void WorkerProc();
static DWORD WINAPI MpqPrefetchWorkerThread(LPVOID) {
    WorkerProc();
    return 0;
}
static std::queue<std::string> g_prefetchQueue;
static WinMutex g_queueMutex;
static WinCondVar g_queueCv;
static std::atomic<bool> g_workerShutdown{false};

// ================================================================
// Prefetch Cache (track which files we've already prefetched)
// ================================================================
static std::unordered_set<std::string> g_prefetchedFiles;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

// ================================================================
// Statistics (atomic counters)
// ================================================================
static volatile LONG g_filesQueued = 0;
static volatile LONG g_filesCompleted = 0;
static volatile LONG g_filesDropped = 0;
static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static volatile LONG g_zoneTransitions = 0;
static double g_totalPrefetchTimeMs = 0.0;
static SRWLOCK g_prefetchTimeLock = SRWLOCK_INIT;

static constexpr int WORKER_THREAD_COUNT = 2;
static double g_qpcFreqMs = 0.0;
static bool g_initialized = false;

// ================================================================
// Hook definitions
// ================================================================
typedef int (__cdecl *sub_5204C0_fn)(int a1, int a2, int a3, const char* a4, const char* a5, const char* a6, int a7);
static sub_5204C0_fn orig_sub_5204C0 = nullptr;

static void OnZoneChange(int newZone);

static int __cdecl Hooked_sub_5204C0(int a1, int a2, int a3, const char* a4, const char* a5, const char* a6, int a7) {
    if (a1 > 0) {
        OnZoneChange(a1);
    }
    return orig_sub_5204C0(a1, a2, a3, a4, a5, a6, a7);
}

// ================================================================
// Zone File Mapping Initialization
// ================================================================
static void InitializeZoneFileMap() {
    // Icecrown Citadel (zone 4812)
    g_zoneFileMap[4812] = {
        "World\\Maps\\IcecrownCitadel\\IcecrownCitadel.wdt",
        "World\\Maps\\IcecrownCitadel\\IcecrownCitadel_tex0.adt",
        "World\\Wmo\\Northrend\\IcecrownRaid\\icecrown_raid.wmo",
        "Creature\\LichKing\\LichKing.m2",
        "Creature\\Sindragosa\\Sindragosa.m2",
        "Sound\\Spells\\IceCrown_Sindragosa_Hurt.wav",
        "Sound\\Spells\\Fizzle.wav"
    };

    // Dalaran (zone 4395)
    g_zoneFileMap[4395] = {
        "World\\Maps\\Northrend\\Dalaran.wdt",
        "World\\Maps\\Northrend\\Dalaran_tex0.adt",
        "World\\Wmo\\Northrend\\Dalaran\\dalaran.wmo",
        "Creature\\DalaranGuard\\DalaranGuard.m2",
        "Sound\\Spells\\SpellCastHorde.wav",
        "Sound\\Spells\\SpellCastAlliance.wav",
        "Sound\\Spells\\Fizzle.wav"
    };

    // Orgrimmar (zone 1637)
    g_zoneFileMap[1637] = {
        "World\\Maps\\Kalimdor\\Kalimdor.wdt",
        "World\\Maps\\Kalimdor\\Kalimdor_30_47.adt",
        "World\\Wmo\\Kalimdor\\Orgrimmar\\orgrimmar.wmo",
        "Creature\\OrcM\\OrcM.m2"
    };

    // Stormwind (zone 1519)
    g_zoneFileMap[1519] = {
        "World\\Maps\\Azeroth\\Azeroth.wdt",
        "World\\Maps\\Azeroth\\Azeroth_32_49.adt",
        "World\\Wmo\\Azeroth\\Stormwind\\stormwind.wmo",
        "Creature\\HumanM\\HumanM.m2"
    };

    Log("[MPQPrefetch] Initialized zone file map with %d zones (including M2/WMO files)", (int)g_zoneFileMap.size());
}

// ================================================================
// Zone Transition Prediction
// ================================================================
static int PredictNextZone(int currentZone) {
    // Dalaran → ICC (common raid teleport)
    if (currentZone == 4395) return 4812;
    
    // ICC → Dalaran (return from raid)
    if (currentZone == 4812) return 4395;
    
    // Orgrimmar → Dalaran (zeppelin)
    if (currentZone == 1637) return 4395;
    
    // Stormwind → Dalaran (boat)
    if (currentZone == 1519) return 4395;
    
    return 0;
}

// Load Storm APIs
static bool LoadStormAPIs() {
    HMODULE hStorm = GetModuleHandleA("storm.dll");
    if (!hStorm) hStorm = LoadLibraryA("storm.dll");
    if (!hStorm) return false;

    pSFileOpenArchive = (SFileOpenArchive_fn)GetProcAddress(hStorm, "SFileOpenArchive");
    pSFileOpenFileEx = (SFileOpenFileEx_fn)GetProcAddress(hStorm, "SFileOpenFileEx");
    pSFileReadFile = (SFileReadFile_fn)GetProcAddress(hStorm, "SFileReadFile");
    pSFileCloseFile = (SFileCloseFile_fn)GetProcAddress(hStorm, "SFileCloseFile");
    pSFileCloseArchive = (SFileCloseArchive_fn)GetProcAddress(hStorm, "SFileCloseArchive");

    return pSFileOpenArchive && pSFileOpenFileEx && pSFileReadFile && pSFileCloseFile && pSFileCloseArchive;
}

// Open main WoW archives privately to read files on worker thread safely
static void OpenPrivateArchives() {
    const char* archiveNames[] = {
        "Data\\common.MPQ",
        "Data\\world.MPQ",
        "Data\\lichking.MPQ",
        "Data\\patch.MPQ",
        "Data\\patch-2.MPQ",
        "Data\\patch-3.MPQ"
    };

    for (const char* name : archiveNames) {
        HANDLE hArchive = nullptr;
        if (pSFileOpenArchive(name, 0, 0, &hArchive)) {
            g_privateArchives.push_back(hArchive);
        }
    }
    Log("[MPQPrefetch] Opened %d MPQ archives privately for background loading", (int)g_privateArchives.size());
}

// ================================================================
// File Prefetching (Worker Thread)
// ================================================================
static void PrefetchFile(const PrefetchRequest* request) {
    // Check if already prefetched
    AcquireSRWLockShared(&g_cacheLock);
    bool cached = (g_prefetchedFiles.find(request->filename) != g_prefetchedFiles.end());
    ReleaseSRWLockShared(&g_cacheLock);

    if (cached) {
        InterlockedIncrement(&g_cacheHits);
        InterlockedIncrement(&g_filesCompleted);
        return;
    }

    InterlockedIncrement(&g_cacheMisses);

    // Try to open the file from our private handles
    char readBuffer[65536];
    bool opened = false;
    for (HANDLE hArchive : g_privateArchives) {
        HANDLE hFile = nullptr;
        if (pSFileOpenFileEx(hArchive, request->filename, 0, &hFile)) {
            opened = true;
            // Force load into OS page cache
            DWORD bytesRead = 0;
            while (pSFileReadFile(hFile, readBuffer, sizeof(readBuffer), &bytesRead, NULL) && bytesRead > 0) {
                // Just reading
            }
            pSFileCloseFile(hFile);
            Log("[MPQPrefetch] Background loaded from MPQ: '%s'", request->filename);
            
            // Mark as prefetched
            AcquireSRWLockExclusive(&g_cacheLock);
            g_prefetchedFiles.insert(request->filename);
            ReleaseSRWLockExclusive(&g_cacheLock);
            break;
        }
    }

    if (!opened) {
        Log("[MPQPrefetch] WARNING: Failed to find file in private MPQs: '%s'", request->filename);
    }

    InterlockedIncrement(&g_filesCompleted);
}

// ================================================================
// Worker Thread Procedure
// ================================================================
static void WorkerProc() {
    Log("[MPQPrefetch] Worker thread started");

    while (!g_workerShutdown.load(std::memory_order_relaxed)) {
        std::string filename;
        {
            WinUniqueLock lock(g_queueMutex);
            g_queueCv.wait_for(lock, std::chrono::milliseconds(20), [] {
                return !g_prefetchQueue.empty() || g_workerShutdown.load();
            });
            if (g_workerShutdown.load() && g_prefetchQueue.empty()) break;
            if (g_prefetchQueue.empty()) continue;
            filename = std::move(g_prefetchQueue.front());
            g_prefetchQueue.pop();
        }

        PrefetchRequest req;
        strncpy_s(req.filename, sizeof(req.filename), filename.c_str(), _TRUNCATE);
        req.priority = 1;
        req.predictedZone = 0;

        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);

        PrefetchFile(&req);

        QueryPerformanceCounter(&end);
        double prefetchTimeMs = (double)(end.QuadPart - start.QuadPart) / g_qpcFreqMs;

        AcquireSRWLockExclusive(&g_prefetchTimeLock);
        g_totalPrefetchTimeMs += prefetchTimeMs;
        ReleaseSRWLockExclusive(&g_prefetchTimeLock);
    }

    Log("[MPQPrefetch] Worker thread exiting");
}

// ================================================================
// Zone Change Detection and Prefetch Triggering
// ================================================================
static void OnZoneChange(int newZone) {
    if (newZone == g_currentZone) return;

    Log("[MPQPrefetch] Zone change detected: %d → %d", g_currentZone, newZone);

    // Record transition
    AcquireSRWLockExclusive(&g_zoneLock);
    
    ZoneTransition& trans = g_zoneHistory[g_zoneHistoryHead];
    trans.fromZone = g_currentZone;
    trans.toZone = newZone;
    trans.timestamp = GetTickCount();
    
    g_zoneHistoryHead = (g_zoneHistoryHead + 1) % ZONE_HISTORY_SIZE;
    g_currentZone = newZone;
    
    ReleaseSRWLockExclusive(&g_zoneLock);

    InterlockedIncrement(&g_zoneTransitions);

    // Predict next zone and queue files
    int predictedZone = PredictNextZone(newZone);
    if (predictedZone > 0) {
        auto it = g_zoneFileMap.find(predictedZone);
        if (it != g_zoneFileMap.end()) {
            Log("[MPQPrefetch] Predicting zone %d, queuing %d files", 
                predictedZone, (int)it->second.size());

            WinLockGuard lock(g_queueMutex);
            // Clear current queue to prioritize new zone files
            while (!g_prefetchQueue.empty()) g_prefetchQueue.pop();

            for (const auto& filename : it->second) {
                g_prefetchQueue.push(filename);
                InterlockedIncrement(&g_filesQueued);
            }
            g_queueCv.notify_all();
        }
    }
}

// ================================================================
// Public API Implementation
// ================================================================

bool Init() {
    Log("[MPQPrefetch] Init");

    if (!LoadStormAPIs()) {
        Log("[MPQPrefetch] ERROR: Failed to resolve Storm.dll exports");
        return false;
    }

    // Open private archives
    OpenPrivateArchives();

    // Initialize QPC frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreqMs = (double)freq.QuadPart / 1000.0;

    // Initialize zone file map
    InitializeZoneFileMap();

    // Spawn background threads
    g_workerShutdown = false;
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        HANDLE h = CreateThread(NULL, 0, MpqPrefetchWorkerThread, NULL, 0, NULL);
        if (h) g_workerThreads.push_back(h);
    }

    // Install zone change hook on sub_5204C0
    if (MH_CreateHook((void*)0x005204C0, (void*)Hooked_sub_5204C0, (void**)&orig_sub_5204C0) == MH_OK) {
        if (MH_EnableHook((void*)0x005204C0) == MH_OK) {
            Log("[MPQPrefetch] Zone change hook installed successfully at 0x005204C0");
        } else {
            Log("[MPQPrefetch] ERROR: Failed to enable zone change hook");
            Shutdown();
            return false;
        }
    } else {
        Log("[MPQPrefetch] ERROR: Failed to create zone change hook");
        Shutdown();
        return false;
    }

    g_initialized = true;
    Log("[MPQPrefetch] [ OK ] Worker thread pool created (%d threads) and hooks active",
        WORKER_THREAD_COUNT);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("[MPQPrefetch] Shutdown");

    // Disable and remove zone change hook
    MH_DisableHook((void*)0x005204C0);
    MH_RemoveHook((void*)0x005204C0);

    // Signal worker threads to exit
    g_workerShutdown = true;
    g_queueCv.notify_all();

    // Wait for worker threads
    for (HANDLE h : g_workerThreads) {
        if (h) {
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
        }
    }
    g_workerThreads.clear();

    // Close private archive handles
    for (HANDLE hArchive : g_privateArchives) {
        pSFileCloseArchive(hArchive);
    }
    g_privateArchives.clear();

    // Clear cache
    AcquireSRWLockExclusive(&g_cacheLock);
    g_prefetchedFiles.clear();
    ReleaseSRWLockExclusive(&g_cacheLock);

    // Log final stats
    Log("[MPQPrefetch] Final stats: Queued=%d, Completed=%d, Dropped=%d, CacheHits=%d, CacheMisses=%d, ZoneTransitions=%d",
        g_filesQueued, g_filesCompleted, g_filesDropped, g_cacheHits, g_cacheMisses, g_zoneTransitions);

    g_initialized = false;
}

void OnFrame(DWORD mainThreadId) {
    // Hooks handle it asynchronously; no polling required.
}

Stats GetStats() {
    Stats s;
    s.filesQueued = g_filesQueued;
    s.filesCompleted = g_filesCompleted;
    s.filesDropped = g_filesDropped;
    s.cacheHits = g_cacheHits;
    s.cacheMisses = g_cacheMisses;
    s.zoneTransitions = g_zoneTransitions;

    WinLockGuard lock(g_queueMutex);
    s.queueDepth = (long)g_prefetchQueue.size();

    AcquireSRWLockShared(&g_prefetchTimeLock);
    s.totalPrefetchTimeMs = g_totalPrefetchTimeMs;
    ReleaseSRWLockShared(&g_prefetchTimeLock);

    return s;
}

void QueuePrefetch(const char* filename) {
    if (!filename || filename[0] == '\0') return;
    PrefetchRequest req;
    strncpy_s(req.filename, sizeof(req.filename), filename, _TRUNCATE);
    req.priority = 1;
    req.predictedZone = 0;

    WinLockGuard lock(g_queueMutex);
    g_prefetchQueue.push(req.filename);
    InterlockedIncrement(&g_filesQueued);
    g_queueCv.notify_one();
}


} // namespace MPQPrefetch
