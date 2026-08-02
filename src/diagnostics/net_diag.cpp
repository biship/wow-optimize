// ============================================================================
// Module: net_diag.cpp
//
// Disconnects are the longest-standing complaint against this project and the
// only one never explained. Reports arrive as "dropped after ten minutes", the
// log around that moment shows nothing unusual, and that is the whole problem:
// nothing records the receive path, so there is no way to tell a server-side
// drop from a client-side one, let alone from something this DLL did.
//
// This is an observer, not an optimisation. It hooks recv, WSARecv and
// closesocket, records four numbers on the hot path, and changes nothing about
// what any of them do or return. Everything it prints happens at the moment a
// connection ends, which is once a session at most.
//
// What it captures at that moment is the part that has been missing:
//
//   - how the connection ended. A graceful zero from the server is a different
//     event from WSAECONNRESET, which is different again from WSAETIMEDOUT, and
//     the player cannot tell them apart while we cannot either.
//   - how long since the last byte actually arrived. A drop after two minutes of
//     silence is the server or the route; a drop while data was still flowing a
//     moment earlier is not.
//   - what this DLL was doing. If the main thread had stalled, or a loading
//     transition was in progress, that is worth knowing before blaming anything.
//
// None of this fixes a disconnect. It is meant to make the next report say which
// of several unrelated things is happening, because at present they all look the
// same from outside.
// ============================================================================

#include <windows.h>
#include <winsock2.h>
#include <cstdint>

#include "net_diag.h"
#include "crash_dumper.h"
#include "config.h"
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);
extern "C" DWORD WowOpt_LastMainThreadTick();

namespace LuaOpt { bool IsLoadingMode(); bool IsReloading(); bool IsSwapping(); }

namespace NetDiag {

typedef int (WINAPI* recv_fn)(SOCKET, char*, int, int);
typedef int (WINAPI* WSARecv_fn)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD,
                                 LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (WINAPI* closesocket_fn)(SOCKET);
typedef int (WINAPI* send_fn)(SOCKET, const char*, int, int);

static recv_fn        orig_recv        = nullptr;
static WSARecv_fn     orig_WSARecv     = nullptr;
static closesocket_fn orig_closesocket = nullptr;
static send_fn        orig_send        = nullptr;

static bool g_active = false;

// Hot path state. Four writes per receive, no locks - these are counters read
// only when something has already gone wrong.
static volatile DWORD  g_lastRecvTick  = 0;
static volatile LONG   g_recvCalls     = 0;
static volatile LONG   g_sendCalls     = 0;
static          uint64_t g_recvBytes   = 0;
static          uint64_t g_sendBytes   = 0;
static volatile DWORD  g_lastSendTick  = 0;
static volatile DWORD  g_firstRecvTick = 0;

// One report per session. A disconnect cascades - recv fails, then send fails,
// then the socket closes - and three copies of the same story is noise.
static volatile LONG g_reported = 0;

static const char* ErrorName(int err) {
    switch (err) {
        case WSAECONNRESET:   return "WSAECONNRESET - the peer reset the connection";
        case WSAECONNABORTED: return "WSAECONNABORTED - aborted locally";
        case WSAETIMEDOUT:    return "WSAETIMEDOUT - no response in time";
        case WSAENETRESET:    return "WSAENETRESET - the route dropped";
        case WSAENOTCONN:     return "WSAENOTCONN - socket was not connected";
        case WSAESHUTDOWN:    return "WSAESHUTDOWN - already shut down this side";
        case WSAEHOSTUNREACH: return "WSAEHOSTUNREACH - no route to host";
        case WSAENETDOWN:     return "WSAENETDOWN - the network went down";
        case WSAEINTR:        return "WSAEINTR - the call was cancelled";
        default:              return "";
    }
}

// `how` describes what ended it; `err` is 0 for a graceful close.
static void ReportDisconnect(const char* how, int err) {
    if (InterlockedCompareExchange(&g_reported, 1, 0) != 0) return;

    DWORD now = GetTickCount();
    DWORD sinceRecv = (g_lastRecvTick != 0) ? (now - g_lastRecvTick) : 0;
    DWORD sinceSend = (g_lastSendTick != 0) ? (now - g_lastSendTick) : 0;
    DWORD sessionMs = (g_firstRecvTick != 0) ? (now - g_firstRecvTick) : 0;

    Log("!!! DISCONNECT !!! %s%s%s", how,
        (err && *ErrorName(err)) ? " - " : "",
        (err && *ErrorName(err)) ? ErrorName(err) : "");

    if (err && !*ErrorName(err)) {
        Log("!!!   Winsock error %d (no name known for it here)", err);
    }

    Log("!!!   Last byte arrived %lu ms ago, last send %lu ms ago",
        (unsigned long)sinceRecv, (unsigned long)sinceSend);
    Log("!!!   Session carried %llu bytes in over %ld receives, %llu bytes out "
        "over %ld sends, across %lu ms",
        (unsigned long long)g_recvBytes, g_recvCalls,
        (unsigned long long)g_sendBytes, g_sendCalls,
        (unsigned long)sessionMs);

    // Whether this DLL was in the middle of something. A drop during a loading
    // transition or while the main thread was not ticking is a different
    // situation from one in steady play, and the two have been indistinguishable
    // in every report so far.
    DWORD lastTick = WowOpt_LastMainThreadTick();
    DWORD mainSilent = (lastTick != 0) ? (now - lastTick) : 0;
    Log("!!!   Main thread last ticked %lu ms ago; loading=%d reloading=%d swapping=%d",
        (unsigned long)mainSilent,
        LuaOpt::IsLoadingMode() ? 1 : 0,
        LuaOpt::IsReloading() ? 1 : 0,
        LuaOpt::IsSwapping() ? 1 : 0);

    CrashDumper::DumpTrace(12, 30000);
    Log("!!! END DISCONNECT REPORT !!!");
}

// Winsock's last-error value belongs to the caller, and everything this observer
// does after the original returns - GetTickCount, and above all Log, which
// writes a file - overwrites it. A hook that reports a disconnect and leaves the
// client reading some unrelated error code is not observing, it is interfering.
// Each hook therefore saves the value the original left behind and restores it
// on the way out, so the client sees exactly what it would have seen.
static int WINAPI Hooked_recv(SOCKET s, char* buf, int len, int flags) {
    int r = orig_recv(s, buf, len, flags);
    DWORD le = GetLastError();
    if (r > 0) {
        DWORD now = GetTickCount();
        g_lastRecvTick = now;
        if (g_firstRecvTick == 0) g_firstRecvTick = now;
        InterlockedIncrement(&g_recvCalls);
        g_recvBytes += (uint64_t)r;
    } else if (r == 0) {
        ReportDisconnect("the server closed the connection cleanly", 0);
    } else {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            ReportDisconnect("recv failed", err);
        }
    }
    SetLastError(le);
    return r;
}

static int WINAPI Hooked_WSARecv(SOCKET s, LPWSABUF bufs, DWORD count,
                                 LPDWORD got, LPDWORD flags,
                                 LPWSAOVERLAPPED ov,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE done) {
    int r = orig_WSARecv(s, bufs, count, got, flags, ov, done);
    DWORD le = GetLastError();
    if (r == 0 && got) {
        if (*got > 0) {
            DWORD now = GetTickCount();
            g_lastRecvTick = now;
            if (g_firstRecvTick == 0) g_firstRecvTick = now;
            InterlockedIncrement(&g_recvCalls);
            g_recvBytes += (uint64_t)*got;
        } else {
            ReportDisconnect("the server closed the connection cleanly", 0);
        }
    } else if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSA_IO_PENDING && err != WSAEINPROGRESS) {
            ReportDisconnect("WSARecv failed", err);
        }
    }
    SetLastError(le);
    return r;
}

static int WINAPI Hooked_send(SOCKET s, const char* buf, int len, int flags) {
    int r = orig_send(s, buf, len, flags);
    DWORD le = GetLastError();
    if (r > 0) {
        g_lastSendTick = GetTickCount();
        InterlockedIncrement(&g_sendCalls);
        g_sendBytes += (uint64_t)r;
    } else if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            ReportDisconnect("send failed", err);
        }
    }
    SetLastError(le);
    return r;
}

static int WINAPI Hooked_closesocket(SOCKET s) {
    // Only interesting if nothing else has explained the end yet. A socket
    // closing during an orderly logout is not a disconnect, so this says so
    // rather than crying wolf.
    if (g_reported == 0 && g_recvCalls > 0) {
        DWORD le = GetLastError();
        ReportDisconnect("the client closed the socket, with no prior error", 0);
        SetLastError(le);
    }
    return orig_closesocket(s);
}

bool Init() {
    if (!Config::g_settings.OptNetDiag) return true;

    HMODULE ws = GetModuleHandleA("ws2_32.dll");
    if (!ws) ws = LoadLibraryA("ws2_32.dll");
    if (!ws) {
        Log("[NetDiag] ws2_32.dll is not loaded - nothing to observe");
        return false;
    }

    struct { const char* name; void* detour; void** orig; } hooks[] = {
        { "recv",        (void*)Hooked_recv,        (void**)&orig_recv        },
        { "WSARecv",     (void*)Hooked_WSARecv,     (void**)&orig_WSARecv     },
        { "send",        (void*)Hooked_send,        (void**)&orig_send        },
        { "closesocket", (void*)Hooked_closesocket, (void**)&orig_closesocket },
    };

    int installed = 0;
    for (auto& h : hooks) {
        void* target = (void*)GetProcAddress(ws, h.name);
        if (!target) continue;
        if (WineSafe_CreateHook(target, h.detour, h.orig) == MH_OK &&
            WO_EnableHook(target) == MH_OK) {
            ++installed;
        } else {
            Log("[NetDiag]   %s could not be hooked", h.name);
        }
    }

    if (installed == 0) {
        Log("[NetDiag] No socket entry points could be hooked");
        return false;
    }

    g_active = true;
    Log("[NetDiag] Watching the receive path, %d/4 entry points - reports once if "
        "the connection ends", installed);
    return true;
}

void LogStats() {
    if (!g_active) return;
    DWORD now = GetTickCount();
    Log("[NetDiag] %ld receives (%llu bytes), %ld sends (%llu bytes), last byte "
        "%lu ms ago",
        g_recvCalls, (unsigned long long)g_recvBytes,
        g_sendCalls, (unsigned long long)g_sendBytes,
        (unsigned long)(g_lastRecvTick ? (now - g_lastRecvTick) : 0));
}

void Shutdown() {
    if (!g_active) return;
    g_active = false;
    LogStats();
}

} // namespace NetDiag
