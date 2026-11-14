#pragma once
#include <cstdint>
#include <cstddef>
// Guarded memory access without SEH: VirtualQuery decides whether a span is
// committed and readable, and ReadProcessMemory on our own process does the
// copy, so a page that vanishes between the two fails the read instead of
// faulting the game thread. The same idea as Stamina Master's mem.cpp, built
// so it compiles with a GNU toolchain that has no __try.
namespace cm::mem
{
    struct Image { uintptr_t base = 0; size_t size = 0; };
    const Image& Game();
    bool InImage(uintptr_t p, size_t n = 1);
    bool Readable(uintptr_t p, size_t n);
    bool Read(uintptr_t p, void* out, size_t n);
    template <class T> inline bool Get(uintptr_t p, T& out) { return Read(p, &out, sizeof(T)); }
    bool PlausibleHeap(uintptr_t p);
    // ".?AVName@pa@@" of the object at obj (or nullptr) via vtable -> COL -> TD.
    const char* RttiOfObject(uintptr_t obj);
    const char* RttiOfVtable(uintptr_t vt);
    // Change one pointer in read-only memory; returns false (and changes
    // nothing) unless *slot == expect.
    bool SwapPointer(uintptr_t slot, uintptr_t expect, uintptr_t value);
}
