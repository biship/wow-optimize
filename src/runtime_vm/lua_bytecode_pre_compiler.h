#pragma once

// ============================================================================
// Module: lua_bytecode_pre_compiler.h
// Description: Accelerates Lua runtime calls in `lua_bytecode_pre_compiler.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










// Background worker that pre-reads Interface\AddOns\**\*.lua and
// WTF\Account\**\*.lua into the OS file cache so the main thread's
// luaL_loadbuffer at /reload time pays no disk latency.

#include <windows.h>
#include <cstdint>

namespace LuaBytecodePreCompiler {

bool Init();
void Shutdown();
void OnFrame();

struct Stats {
    bool     active;
    uint32_t filesScanned;
    uint32_t filesPreloaded;
    uint64_t bytesPreloaded;
    uint32_t workers;
    uint32_t queueDepth;
};
void GetStats(Stats* out);

} // namespace LuaBytecodePreCompiler
