// ============================================================================
// Module: m2_matrix_slot_sse2
//
// sub_82F0F0 is M2_AnimateModel. A tester's sampling profile puts roughly a
// fifth of main-thread execution in the animation family around it, the largest
// single target in the whole profile and about twice anything else.
//
// Two blocks inside it write a bone's finished 4x4 matrix into the model's
// matrix array, one float at a time, as sixteen fld/fstp pairs. They are the
// two arms of one if/else and both write the same slot:
//
//     0x0082FE54  the matrix sub_4C1F00 just produced, EAX -> the slot
//     0x0082FECB  the matrix at [EBX],                  EBX -> the slot
//
// The slot is [ESI+0x98] + (arg_10 << 6): a 64-byte entry in the model's matrix
// array, indexed by the bone number the loop at loc_83028C increments and
// compares against [[EBP-4]+0x2C].
//
// Thirty-two x87 instructions to move sixty-four bytes. Four movups loads and
// four stores do the same thing.
//
// ---------------------------------------------------------------------------
// Why this needs no precision measurement
//
// There is no arithmetic. `fld dword` widens a single to 80 bits and `fstp
// dword` narrows it back, which is exact for every finite value, every
// infinity, and every quiet NaN. `movups` copies the bits. The one input where
// the two differ is a signalling NaN, which fld quiets and a move does not - a
// matrix does not contain one, and a client that put one there would already be
// undefined.
//
// This is the same argument bone_matrix_upload_sse2 shipped on, and the same
// shape: a pure-move block found by the fld/fstp scan inside a function the
// profile had already named.
//
// ---------------------------------------------------------------------------
// The register contracts, read off the disassembly rather than assumed
//
// Site A, 0x0082FE54 through 0x0082FEC3, falls through to 0x0082FEC4:
//
//     in   EAX = source matrix, ESI = model, EBP = frame
//     does the copy, and `add esp, 0Ch` - the cleanup for the three arguments
//          pushed to sub_4C1F00 just above
//     out  EDX = arg_10 << 6, ECX = slot address, ESP raised by twelve
//     x87  sixteen balanced pairs, so net zero
//
// ECX is dead: 0x0082FEC4 is `mov ecx, [ebp+0Ch]`. EAX is dead too. EDX is
// read at 0x0082FF4E on one path and overwritten before use on the other, so it
// is reproduced.
//
// Site B, 0x0082FECB through 0x0082FF39, falls through to 0x0082FF3A:
//
//     in   EBX = source matrix, ESI = model, EBP = frame
//     out  EAX = slot address, EDX = arg_10 << 6
//     x87  one `fstp st(0)` before the copy, so net minus one
//
// That single pop is part of the contract and the replacement performs it. The
// other arm reaches the same label having pushed with `fldz`, and the code at
// 0x0082FF46 pops once, which is what balances the pair of paths.
//
// Neither range is jumped into. Every cross-reference inside site A is ordinary
// flow, and site B has exactly one, the jump to its first byte from 0x0082F7B1.
//
// ---------------------------------------------------------------------------
// What is checked, and what is not
//
// Both ends of both blocks are compared against the bytes they were read from
// before anything is written, so a different client build refuses rather than
// jumps into the middle of an instruction. The five-byte jump is saved and put
// back on shutdown.
//
// The contract above is read, not measured, and a wrong reading would write
// sixty-four bytes somewhere else. So the first call through each site hands
// the slot address, the model and the frame to a plain C function that checks
// the slot is readable and that the bone index is below the count the client's
// own loop compares against. It cannot undo a patch from inside the code the
// patch redirects to, so it does not try: it records the failure and the report
// says the numbers from that session are not to be used.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "m2_matrix_slot_sse2.h"
#include "version.h"
#include "config.h"
#include "ab_test.h"

extern "C" void Log(const char* fmt, ...);

namespace M2MatrixSlot {

namespace {

enum { kSiteA = 0, kSiteB = 1, kSites = 2 };

const unsigned char kHeadA[8] = { 0xD9, 0x00, 0x8B, 0x8E, 0x98, 0x00, 0x00, 0x00 };
const unsigned char kTailA[6] = { 0xD9, 0x40, 0x3C, 0xD9, 0x59, 0x3C };
const unsigned char kHeadB[8] = { 0x8B, 0x86, 0x98, 0x00, 0x00, 0x00, 0xDD, 0xD8 };
const unsigned char kTailB[6] = { 0xD9, 0x43, 0x3C, 0xD9, 0x58, 0x3C };

struct Site {
    const char*          name;
    uintptr_t            head;
    uintptr_t            tailAddr;
    const unsigned char* headWant;
    int                  headLen;
    const unsigned char* tailWant;
    int                  tailLen;
    void*                thunk;
    void*                returnTo;
    bool                 patched;
};

void ThunkA();
void ThunkB();

Site g_site[kSites] = {
    { "sub_82F0F0 site A", 0x0082FE54, 0x0082FEBE, kHeadA, 8, kTailA, 6,
      nullptr, (void*)0x0082FEC4, false },
    { "sub_82F0F0 site B", 0x0082FECB, 0x0082FF34, kHeadB, 8, kTailB, 6,
      nullptr, (void*)0x0082FF3A, false },
};

unsigned char g_saved[kSites][8] = {};

// Named one per site rather than indexed, because the thunks are naked and
// hand-computing a struct offset inside inline assembly is how a jump lands
// somewhere that was never checked.
void* g_retA = (void*)0x0082FEC4;
void* g_retB = (void*)0x0082FF3A;

// Main thread only, so plain. Lower bounds if that ever stops being true.
unsigned long g_callsA = 0;
unsigned long g_callsB = 0;

// The one-time contract check, per site.
unsigned char g_checkedA = 0;
unsigned char g_checkedB = 0;
bool          g_contractFailed  = false;
const char*   g_contractReason  = nullptr;

// The flag the A/B harness owns. False switches both thunks back to a scalar
// copy, which is the same sixty-four bytes moved four at a time rather than
// sixteen - the control half has to write the slot too, or the two halves are
// not comparing the same frame.
bool g_abOn = true;
unsigned long g_scalarCalls = 0;

bool Readable(uintptr_t p) {
    if (p < 0x10000 || p > 0xFFE00000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD bad = PAGE_NOACCESS | PAGE_GUARD;
    return (mbi.Protect & bad) == 0;
}

}  // namespace

// Called once per site, from the thunk, on its first call. Everything it needs
// is what the thunk already had in registers.
extern "C" void __cdecl M2MatrixSlot_CheckContract(int site, void* slot,
                                                   void* model, void* frame) {
    if (site < 0 || site >= kSites) return;
    if (site == 0) { if (g_checkedA) return; g_checkedA = 1; }
    else           { if (g_checkedB) return; g_checkedB = 1; }

    const uintptr_t s = (uintptr_t)slot;
    if (!Readable(s) || !Readable(s + 63)) {
        g_contractFailed = true;
        g_contractReason = "the slot address is not readable";
        Log("[M2Slot] CONTRACT CHECK FAILED at %s: the slot address 0x%08X is "
            "not readable memory, so [ESI+0x98] + (arg_10 << 6) is not what "
            "this thought it was.", g_site[site].name, (unsigned)s);
        return;
    }

    // The client's own loop at 0x0083028C compares the bone index against
    // [[EBP-4]+0x2C]. If the index this read is not below that, the index is
    // not the one the loop is counting.
    const uintptr_t ebp = (uintptr_t)frame;
    if (Readable(ebp - 4)) {
        const uintptr_t owner = *(const uintptr_t*)(ebp - 4);
        if (Readable(owner + 0x2C)) {
            const unsigned count = *(const unsigned*)(owner + 0x2C);
            const unsigned index = *(const unsigned*)(ebp + 0x18);
            if (count == 0 || index >= count) {
                g_contractFailed = true;
                g_contractReason = "the bone index is not below the bone count";
                Log("[M2Slot] CONTRACT CHECK FAILED at %s: bone index %u is not "
                    "below the count %u the client's own loop compares against.",
                    g_site[site].name, index, count);
                return;
            }
        }
    }

    (void)model;
    Log("[M2Slot] %s: contract checked on the first call - slot 0x%08X is "
        "readable and the bone index is inside the count.",
        g_site[site].name, (unsigned)s);
}

namespace {

// Both thunks are entered by a jump, not a call, so there is no return address
// of ours on the stack and every exit is a jump to the instruction after the
// block. Nothing is pushed that is not popped.
__declspec(naked) void ThunkA() {
    __asm {
        // EDX = arg_10 << 6, ECX = [ESI+0x98] + EDX, exactly as the block did.
        mov  edx, [ebp+18h]
        shl  edx, 6
        mov  ecx, [esi+98h]
        add  ecx, edx

        cmp  byte ptr [g_checkedA], 0
        jne  a_checked
        pushad
        push ebp
        push esi
        push ecx
        push 0
        call M2MatrixSlot_CheckContract
        add  esp, 16
        popad
    a_checked:

        cmp  byte ptr [g_abOn], 0
        je   a_scalar

        movups xmm0, [eax]
        movups xmm1, [eax+16]
        movups xmm2, [eax+32]
        movups xmm3, [eax+48]
        movups [ecx], xmm0
        movups [ecx+16], xmm1
        movups [ecx+32], xmm2
        movups [ecx+48], xmm3
        inc  dword ptr [g_callsA]
        jmp  a_done

    a_scalar:
        // The control half of an A/B stint. Four bytes at a time, which is what
        // the client's x87 pair moved, without the x87.
        push esi
        push edi
        mov  esi, eax
        mov  edi, ecx
        mov  eax, 16
    a_loop:
        mov  edx, [esi]
        mov  [edi], edx
        add  esi, 4
        add  edi, 4
        dec  eax
        jnz  a_loop
        pop  edi
        pop  esi
        inc  dword ptr [g_scalarCalls]
        // ESI was restored, so recompute what the block promised.
        mov  edx, [ebp+18h]
        shl  edx, 6

    a_done:
        add  esp, 0Ch
        jmp  dword ptr [g_retA]
    }
}

__declspec(naked) void ThunkB() {
    __asm {
        fstp st(0)

        mov  edx, [ebp+18h]
        shl  edx, 6
        mov  eax, [esi+98h]
        add  eax, edx

        cmp  byte ptr [g_checkedB], 0
        jne  b_checked
        pushad
        push ebp
        push esi
        push eax
        push 1
        call M2MatrixSlot_CheckContract
        add  esp, 16
        popad
    b_checked:

        cmp  byte ptr [g_abOn], 0
        je   b_scalar

        movups xmm0, [ebx]
        movups xmm1, [ebx+16]
        movups xmm2, [ebx+32]
        movups xmm3, [ebx+48]
        movups [eax], xmm0
        movups [eax+16], xmm1
        movups [eax+32], xmm2
        movups [eax+48], xmm3
        inc  dword ptr [g_callsB]
        jmp  b_done

    b_scalar:
        push esi
        push edi
        mov  esi, ebx
        mov  edi, eax
        mov  ecx, 16
    b_loop:
        mov  edx, [esi]
        mov  [edi], edx
        add  esi, 4
        add  edi, 4
        dec  ecx
        jnz  b_loop
        pop  edi
        pop  esi
        inc  dword ptr [g_scalarCalls]
        // ECX and EDX were used; EAX still holds the slot. Recompute EDX, and
        // ECX is dead here because 0x0082FF3A reloads it from the frame.
        mov  edx, [ebp+18h]
        shl  edx, 6

    b_done:
        jmp  dword ptr [g_retB]
    }
}

bool BytesMatch(uintptr_t addr, const unsigned char* want, int len) {
    if (!Readable(addr) || !Readable(addr + (uintptr_t)len - 1)) return false;
    return memcmp((const void*)addr, want, (size_t)len) == 0;
}

bool PatchSite(int i) {
    Site& s = g_site[i];
    if (!BytesMatch(s.head, s.headWant, s.headLen)) {
        Log("[M2Slot] %s NOT patched: the bytes at 0x%08X are not the copy this "
            "was read from.", s.name, (unsigned)s.head);
        return false;
    }
    if (!BytesMatch(s.tailAddr, s.tailWant, s.tailLen)) {
        Log("[M2Slot] %s NOT patched: the head at 0x%08X matches but the tail at "
            "0x%08X does not, so the block between them is not the one being "
            "replaced.", s.name, (unsigned)s.head, (unsigned)s.tailAddr);
        return false;
    }
    if (!WowOpt_ClientPatchAllowed((const void*)s.head)) {
        Log("[M2Slot] %s NOT patched: No Client Patches is on, and this writes "
            "five bytes into wow.exe like every hook here does.", s.name);
        return false;
    }

    DWORD old = 0;
    if (!VirtualProtect((void*)s.head, (SIZE_T)s.headLen, PAGE_EXECUTE_READWRITE, &old)) {
        Log("[M2Slot] %s NOT patched: could not make 0x%08X writable",
            s.name, (unsigned)s.head);
        return false;
    }
    memcpy(g_saved[i], (const void*)s.head, (size_t)s.headLen);

    unsigned char patch[8];
    patch[0] = 0xE9;
    *(int32_t*)(patch + 1) = (int32_t)((uintptr_t)s.thunk - (s.head + 5));
    // Whatever follows the jump inside the saved run is the tail of the
    // instruction it split. Unreachable either way; filled so anything reading
    // the code sees nops rather than half of a mov.
    memset(patch + 5, 0x90, (size_t)(s.headLen - 5));
    memcpy((void*)s.head, patch, (size_t)s.headLen);

    DWORD ignored = 0;
    VirtualProtect((void*)s.head, (SIZE_T)s.headLen, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (void*)s.head, (SIZE_T)s.headLen);

    s.patched = true;
    return true;
}

}  // namespace

bool Install() {
    if (!Config::g_settings.OptM2MatrixSlotSse2) {
        Log("[M2Slot] not installed: switched off.");
        return false;
    }

    g_site[kSiteA].thunk = (void*)&ThunkA;
    g_site[kSiteB].thunk = (void*)&ThunkB;

    int done = 0;
    for (int i = 0; i < kSites; ++i)
        if (PatchSite(i)) ++done;

    if (done == 0) {
        Log("[M2Slot] not installed: neither copy block matched the bytes it "
            "was read from.");
        return false;
    }

    // The return says whether this is the subject being alternated right now,
    // which in a rotating run is false for everything but one slot. What the
    // module needs from it is the flag, and the report reads the scalar count
    // rather than a snapshot taken here.
    AbTest::IsSubject("M2MatrixSlotSse2", &g_abOn);
    Log("[M2Slot] ACTIVE on %d of %d sites in sub_82F0F0. Each replaces sixteen "
        "fld/fstp pairs with four SSE2 loads and four stores; there is no "
        "arithmetic in either, so the bytes written are the bytes read.%s",
        done, (int)kSites,
        Config::g_settings.OptAbTest
            ? " The A/B harness owns the switch between the two halves."
            : "");
    return true;
}

void Shutdown() {
    for (int i = 0; i < kSites; ++i) {
        Site& s = g_site[i];
        if (!s.patched) continue;
        DWORD old = 0;
        if (VirtualProtect((void*)s.head, (SIZE_T)s.headLen,
                           PAGE_EXECUTE_READWRITE, &old)) {
            memcpy((void*)s.head, g_saved[i], (size_t)s.headLen);
            DWORD ignored = 0;
            VirtualProtect((void*)s.head, (SIZE_T)s.headLen, old, &ignored);
            FlushInstructionCache(GetCurrentProcess(), (void*)s.head,
                                  (SIZE_T)s.headLen);
        }
        s.patched = false;
    }
}

void LogStats() {
    if (!Config::g_settings.OptM2MatrixSlotSse2) {
        Log("[M2Slot] not measured: switched off.");
        return;
    }
    int patched = 0;
    for (int i = 0; i < kSites; ++i) if (g_site[i].patched) ++patched;
    if (patched == 0) {
        Log("[M2Slot] not measured: neither site is patched.");
        return;
    }

    if (g_contractFailed) {
        Log("[M2Slot] DO NOT USE THIS SESSION'S NUMBERS: the first-call contract "
            "check failed because %s. The register contract was read off the "
            "disassembly and something about it is wrong.", g_contractReason);
    }

    const unsigned long total = g_callsA + g_callsB;
    if (total == 0 && g_scalarCalls == 0) {
        Log("[M2Slot] measured and zero: %d of %d sites patched and neither was "
            "reached. The client animated no model through this path.",
            patched, (int)kSites);
        return;
    }

    Log("[M2Slot] %lu matrix slot(s) written, %lu at site A and %lu at site B, "
        "%d of %d sites patched. Counts are plain increments on the animation "
        "path and are lower bounds.",
        total, g_callsA, g_callsB, patched, (int)kSites);
    if (g_site[kSiteA].patched && g_callsA == 0)
        Log("[M2Slot]   %s is patched and was never reached.", g_site[kSiteA].name);
    if (g_site[kSiteB].patched && g_callsB == 0)
        Log("[M2Slot]   %s is patched and was never reached.", g_site[kSiteB].name);
    if (g_scalarCalls) {
        Log("[M2Slot]   the A/B control half moved %lu slot(s) four bytes at a "
            "time, so both halves wrote the slot and the comparison is between "
            "two ways of doing it rather than doing it and not.", g_scalarCalls);
    } else if (Config::g_settings.OptAbTest) {
        Log("[M2Slot]   the A/B harness never handed this an OFF stint, so "
            "every slot above went out through the SSE2 path and there is no "
            "control half to compare against.");
    }
}

}  // namespace M2MatrixSlot
