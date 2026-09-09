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

// --- Taint -------------------------------------------------------------------
//
// A constant's taint is not stamped from the current context when it is added.
// addk (0x00861F80) copies all four words of the token's TValue into f->k[n],
// taint included, and then - if the constant carried one - writes it back into
// the global at dword_D4139C. So the taint of a constant belongs to the moment
// the chunk was first compiled, the global moves during a parse as constants
// are added, and neither can be reconstructed later. lua_dump does not write it
// out either: DumpConstants stores a tag and a value and nothing else.
//
// That leaves one sound rule, and it is what these three exist for. A chunk is
// only kept if every constant in it, and every constant in every nested
// function, has a taint of zero; it is only replayed while the current taint is
// zero as well; and it is rebuilt with zeros. Then the Proto handed back is the
// one a parse in that same context would have produced, which is a thing that
// can be proved rather than hoped for.
//
// What that gives up is chunks compiled from inside an addon. What it keeps is
// the game's own interface code, which is the part that costs seconds.
unsigned long CurrentTaintValue();

// The Proto under the Lua function on top of L's stack, or null if the top is
// not a Lua function.
void* ProtoOnStackTop(void* L);

// True when this Proto and everything nested inside it has no tainted constant.
bool ProtoIsUntainted(void* proto);

// Stops Load from returning anything for the rest of the session.
void Retire(const char* why);
bool Retired();

void LogStats();

}  // namespace LuaUndump
