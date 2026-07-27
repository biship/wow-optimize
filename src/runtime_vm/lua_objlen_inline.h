#pragma once

// ============================================================================
// Module: lua_objlen_inline.h
// Description: Accelerates Lua runtime calls in `lua_objlen_inline.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









bool InstallLuaObjLenInline();
void UninstallLuaObjLenInline();