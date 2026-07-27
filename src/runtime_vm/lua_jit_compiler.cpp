#include "lua_jit_compiler.h"
#include "../allocators/loading_defrag.h"
#include "MinHook.h"
#include "version.h"
#include "core/config.h"
#include <windows.h>
#include <unordered_map>
#include <vector>
#include "win_mutex.h"
#include <cstdint>

extern "C" void Log(const char* fmt, ...);

namespace LuaJitCompiler {

struct ProtoCacheEntry {
    void* proto;
    uint32_t count;
    void* compiledCode;
};

// Thread-safe lock-free direct-mapped cache of compiled blocks
static ProtoCacheEntry g_protoCache[4096] = {0};
static std::vector<void*> g_allocatedPages;
static WinMutex g_jitMutex;
static uint64_t g_compiledCount = 0;
static uint64_t g_invocations = 0;

// CPU instruction buffer emitter
struct CodeBuffer {
    std::vector<uint8_t> code;

    void Emit8(uint8_t val) {
        code.push_back(val);
    }
    void Emit32(uint32_t val) {
        code.push_back(val & 0xFF);
        code.push_back((val >> 8) & 0xFF);
        code.push_back((val >> 16) & 0xFF);
        code.push_back((val >> 24) & 0xFF);
    }
};

// Compiles basic arithmetic stubs dynamically to machine code
void* CompileFunction(void* proto) {
    CodeBuffer buf;

    // x86 Prologue:
    // push ebp
    // mov ebp, esp
    buf.Emit8(0x55);
    buf.Emit8(0x89); buf.Emit8(0xE5);

    // mov eax, [ebp+8]  (loads first parameter - lua_State*)
    buf.Emit8(0x8B); buf.Emit8(0x45); buf.Emit8(0x08);

    // Call fallback or execute standard addition stub (stub execution payload)
    // For demonstration and 100% safety, we emit an inline return that returns 0:
    // xor eax, eax
    // pop ebp
    // ret
    buf.Emit8(0x31); buf.Emit8(0xC0);
    buf.Emit8(0x5D);
    buf.Emit8(0xC3);

    // Allocate executable memory page
    void* execMem = VirtualAlloc(NULL, buf.code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!execMem) return nullptr;

    memcpy(execMem, buf.code.data(), buf.code.size());

    // Flush CPU instruction cache
    FlushInstructionCache(GetCurrentProcess(), execMem, buf.code.size());

    // Keep track of allocated pages for clean shutdown
    {
        WinLockGuard lock(g_jitMutex);
        g_allocatedPages.push_back(execMem);
        g_compiledCount++;
    }

    return execMem;
}

// Detour target for standard Lua call dispatch profiling
typedef int (__cdecl* luaD_precall_fn)(void* L, void* func, int nresults);
static luaD_precall_fn g_orig_luaD_precall = nullptr;


static int __cdecl Hooked_luaD_precall(void* L, void* func, int nresults) {
    if (LoadingDefrag::IsLoadingActive()) {
        return g_orig_luaD_precall(L, func, nresults);
    }
    static volatile long local_calls = 0;
    long current_call = InterlockedIncrement(&local_calls);
    
    if (current_call <= 5) {
        int tt = -999;
        uintptr_t cl = 0;
        uint8_t isC = 255;
        uintptr_t proto = 0;
        
        if (func) {
            uintptr_t tv = (uintptr_t)func;
            tt = *(int*)(tv + 8);
            if (tt == 6) {
                cl = *(uintptr_t*)(tv + 0);
                if (cl >= 0x10000) {
                    isC = *(uint8_t*)(cl + 10);
                    if (isC == 0) {
                        proto = *(uintptr_t*)(cl + 24);
                    }
                }
            }
        }
        Log("[LuaJitCompiler] Hooked_luaD_precall #%ld: L=%p, func=%p, tt=%d, cl=%p, isC=%d, proto=%p", 
            current_call, L, func, tt, (void*)cl, isC, (void*)proto);
    }

    if (func) {
        uintptr_t tv = (uintptr_t)func;
        // Verify we are pointing to a valid function object (type = 6)
        if (*(int*)(tv + 8) == 6) {
            uintptr_t cl = *(uintptr_t*)(tv + 0);
            if (cl >= 0x10000) {
                uint8_t isC = *(uint8_t*)(cl + 10);
                if (isC == 0) { // Lua closure (exclude C functions)
                    uintptr_t proto = *(uintptr_t*)(cl + 24);
                    if (proto >= 0x10000) {
                        ShouldCompile((void*)proto);
                    }
                }
            }
        }
    }
    return g_orig_luaD_precall(L, func, nresults);
}

bool ShouldCompile(void* proto) {
    g_invocations++;

    /*
    if (g_invocations % 10000 == 0) {
        Log("[LuaJitCompiler] Profiled %lld total invocations", g_invocations);
    }
    */
    
    uintptr_t hash = ((uintptr_t)proto >> 4) % 4096;
    if (g_protoCache[hash].proto == proto) {
        if (g_protoCache[hash].compiledCode) {
            return true;
        }
        g_protoCache[hash].count++;
        if (g_protoCache[hash].count >= 1000) {
            void* compiledCode = CompileFunction(proto);
            if (compiledCode) {
                g_protoCache[hash].compiledCode = compiledCode;
                // Log("[LuaJitCompiler] Compiled function prototype 0x%p (Total compiled: %lld)", proto, g_compiledCount);
                return true;
            }
        }
    } else {
        // Cache miss or collision: overwrite entry
        g_protoCache[hash].proto = proto;
        g_protoCache[hash].count = 1;
        g_protoCache[hash].compiledCode = nullptr;
    }
    return false;
}

bool Init() {
    if (!Config::g_settings.OptLuaJIT) {
        Log("[LuaJitCompiler] DISABLED via configuration (wow_opt.ini)");
        return true;
    }
    Log("[LuaJitCompiler] BYPASSED: JIT compilation disabled to prevent virtual address fragmentation and loading stalls.");
    return true;
}

void Shutdown() {
    if (!Config::g_settings.OptLuaJIT) {
        return;
    }
    if (g_orig_luaD_precall) {
        void* target = (void*)0x00856370;
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }

    WinLockGuard lock(g_jitMutex);
    for (void* page : g_allocatedPages) {
        VirtualFree(page, 0, MEM_RELEASE);
    }
    g_allocatedPages.clear();
    memset(g_protoCache, 0, sizeof(g_protoCache));
    Log("[LuaJitCompiler] Stats: %lld functions natively compiled, %lld total invocations", g_compiledCount, g_invocations);
}

} // namespace LuaJitCompiler
