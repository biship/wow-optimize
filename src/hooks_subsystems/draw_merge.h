#pragma once

// ============================================================================
// The draw-call merger.
//
// The census that measures whether this is worth doing lives in
// d3d9_state_cache.cpp and so does the merger itself, because both need the
// DrawIndexedPrimitive trampoline. This header carries only the two symbols the
// rest of the renderer has to touch: the flush, and the barrier that every
// state change goes through.
//
// Holding a draw call is only safe if everything that could change what it
// produces flushes it first. That is exactly the set of things that moves
// g_stateEpoch, so the barrier does both and there is one place to get wrong
// instead of two.
// ============================================================================

// Bumped where a setter actually reaches D3D9 - never on a deduped skip, since
// a skipped call means the state did not change.
extern "C" unsigned long g_stateEpoch;

// 1 while a draw is being held. Read inline and from the naked barrier thunks
// so the common case costs one byte compare and no call.
extern "C" unsigned char g_drawMergePending;

// Issues the held draw. Safe to call when nothing is held.
extern "C" void __cdecl D3D9DrawMerge_FlushPending(void);

// One state change: flush what is held, then move the epoch. Returns the new
// epoch so it drops straight into the `(++g_stateEpoch, g_orig_X)(...)` form
// the wrapped setters use.
static inline unsigned long D3D9_StateBarrier(void) {
    if (g_drawMergePending) D3D9DrawMerge_FlushPending();
    return ++g_stateEpoch;
}

// A vertex or index buffer was locked. NOOVERWRITE cannot change what an
// already-issued range reads, so it is not a barrier; anything else is.
// Called from the naked Lock thunks in d3d9_state_manager.cpp.
extern "C" void __cdecl D3D9DrawMerge_BufferLockBarrier(unsigned long flags);

// A texture was locked. D3DLOCK_READONLY cannot change a pixel, so it is not a
// barrier; anything else rewrites content a held draw may already read from.
// Called from the naked texture Lock thunks in d3d9_state_manager.cpp.
extern "C" void __cdecl D3D9DrawMerge_TextureLockBarrier(unsigned long flags);

// The client created a state block, which can change device state without ever
// touching the device vtable. Merging stops for the rest of the session.
extern "C" void __cdecl D3D9DrawMerge_Disable(void);
