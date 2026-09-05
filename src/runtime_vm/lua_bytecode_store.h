#pragma once

#include <cstddef>

// A disk store of compiled Lua chunks, so a chunk compiled in one session does
// not have to be compiled again in the next one.
//
// Capture uses the client's own lua_dump; replay uses LuaUndump, because this
// client cannot load bytecode itself. The proto cache beside this file drives
// both: it already knows the identity of a chunk, and it already owns the two
// hooks this needs. See the .cpp.

namespace LuaBytecodeStore {

bool Init();
void Shutdown();

// Rebuilds this chunk's Proto from the store, or returns null. *needsCheck is
// set when the caller must parse the source as well and pass both Protos to
// Confirm before using either - which is what the first few thousand hits do,
// and one in every few hundred after that.
void* Lookup(void* L, const char* src, size_t srcLen, const char* name,
             size_t nameLen, bool* needsCheck);

// Compares a Proto from Lookup against one the client parsed from the same
// source. False means they differ, the store has retired, and the caller must
// use the client's Proto.
bool Confirm(void* mine, void* fresh, const char* name);

// Called with the freshly compiled closure on top of L's stack.
void Capture(void* L, const char* src, size_t srcLen, const char* name,
             size_t nameLen);

// The proto cache measures what an average parse costs. Without it the report
// can say how many parses were skipped but not what they were worth.
void NoteParseCost(double msPerParse);

// Writes the index trailer if anything was captured since the last time, and
// not more than once every half minute. Called from the periodic report, which
// is the only thing here that reliably runs.
void SaveIfDirty();

void OnLuaStateSwap();
void LogStats();

}  // namespace LuaBytecodeStore
