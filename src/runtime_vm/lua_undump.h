#pragma once

#include <cstddef>

// Rebuilds a Proto from the bytes the client's own lua_dump produced.
//
// The client can write bytecode and cannot read it back: f_parser at 0x00856190
// has no signature lookahead and no undump call, so luaY_parser is the only way
// a chunk becomes a Proto. This supplies the missing direction, which is what
// lets a compiled chunk outlive the process it was compiled in.
//
// See the .cpp for the format, every client entry point it calls, and why the
// verification below is a proof rather than a spot check.

namespace LuaUndump {

// True when every client address this needs is readable.
bool Available();

// Builds a Proto from `len` bytes of client dump output. Returns null if the
// buffer is not a dump this client could have written, if it is malformed in
// any way, or if the module has retired. Nothing is allocated until the whole
// buffer has been walked once and found sound.
void* Load(void* L, const void* data, size_t len);

// Full structural comparison of two Protos, recursing into nested ones and
// comparing constants by tag, taint and value. Used by the learning phase to
// check an undumped Proto against one the client parsed from the same source.
// On a difference, *what names the field.
bool Equal(void* a, void* b, const char** what);

// After Equal returns false, the numbers behind *what, or an empty string when
// the difference had none. Valid until the next Equal.
const char* LastDetail();

// Stops Load from returning anything for the rest of the session.
void Retire(const char* why);
bool Retired();

void LogStats();

}  // namespace LuaUndump
