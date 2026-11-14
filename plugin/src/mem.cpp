#include "mem.h"
#include <windows.h>
#include <cstring>
namespace cm::mem
{
    static Image g_img;
    const Image& Game()
    {
        if (!g_img.base)
        {
            auto b = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
            auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(b + dos->e_lfanew);
            g_img.base = b; g_img.size = nt->OptionalHeader.SizeOfImage;
        }
        return g_img;
    }
    bool InImage(uintptr_t p, size_t n) { const auto& g = Game(); return p >= g.base && p + n <= g.base + g.size && p + n >= p; }

    // A tiny region cache: the BFS touches a handful of heap regions over and over.
    struct Region { uintptr_t lo, hi; };
    static Region g_cache[16]; static unsigned g_next = 0;
    static SRWLOCK g_lock = SRWLOCK_INIT;
    bool Readable(uintptr_t p, size_t n)
    {
        if (!p || p + n < p) return false;
        AcquireSRWLockShared(&g_lock);
        for (auto& r : g_cache) if (p >= r.lo && p + n <= r.hi) { ReleaseSRWLockShared(&g_lock); return true; }
        ReleaseSRWLockShared(&g_lock);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & ok) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        uintptr_t lo = reinterpret_cast<uintptr_t>(mbi.BaseAddress), hi = lo + mbi.RegionSize;
        if (p + n > hi) return false;
        AcquireSRWLockExclusive(&g_lock);
        g_cache[g_next++ & 15] = { lo, hi };
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    bool Read(uintptr_t p, void* out, size_t n)
    {
        if (!Readable(p, n)) return false;
        SIZE_T got = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(p), out, n, &got) && got == n;
    }
    bool PlausibleHeap(uintptr_t p) { return p >= 0x10000 && p < 0x00007FFFFFFFFFFFull && (p & 7) == 0; }

    const char* RttiOfVtable(uintptr_t vt)
    {
        const auto& g = Game();
        uintptr_t col = 0;
        if (!InImage(vt - 8, 8) || !Get(vt - 8, col) || !InImage(col, 0x18)) return nullptr;
        uint32_t sig = 0, td = 0, self = 0;
        if (!Get(col, sig) || sig != 1 || !Get(col + 0x0C, td) || !Get(col + 0x14, self)) return nullptr;
        if (g.base + self != col) return nullptr;              // x64 COL points back at itself
        uintptr_t name = g.base + td + 0x10;
        if (!InImage(name, 5)) return nullptr;
        char head[4];
        if (!Read(name, head, 4) || memcmp(head, ".?AV", 4) != 0) return nullptr;
        return reinterpret_cast<const char*>(name);
    }
    const char* RttiOfObject(uintptr_t obj)
    {
        uintptr_t vt = 0;
        if (!PlausibleHeap(obj) || !Get(obj, vt) || !InImage(vt, 8)) return nullptr;
        return RttiOfVtable(vt);
    }
    bool SwapPointer(uintptr_t slot, uintptr_t expect, uintptr_t value)
    {
        uintptr_t cur = 0;
        if (!Get(slot, cur) || cur != expect) return false;
        DWORD old = 0;
        if (!VirtualProtect(reinterpret_cast<LPVOID>(slot), 8, PAGE_READWRITE, &old)) return false;
        // One aligned 8-byte store is atomic on x64, so a thread calling
        // through the slot sees the old or the new target, never half.
        InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), (LONG64)value, (LONG64)expect);
        DWORD tmp; VirtualProtect(reinterpret_cast<LPVOID>(slot), 8, old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(slot), 8);
        return Get(slot, cur) && cur == value;
    }
}
