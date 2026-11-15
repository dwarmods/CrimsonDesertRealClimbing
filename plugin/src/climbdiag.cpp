// Climbmod ASI, diagnostic build. See README.md for what is proven and what is not.
#include "climbdiag.h"
#include "build_info.h"
#include "log.h"
#include "mem.h"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <unordered_set>

extern "C" uintptr_t cm_orig[8] = {};
extern "C" void cm_thunk0(); extern "C" void cm_thunk1(); extern "C" void cm_thunk2();
extern "C" void cm_thunk3(); extern "C" void cm_thunk4();

namespace cm
{
namespace
{
    using namespace cm::build;
    constexpr unsigned kSlots = kSlotCount;
    const BuildDef* g_b = nullptr; // selected by IdentityOk, never null after it succeeds
    static_assert(kSlots == 5, "one thunk per slot");
    void (*const kThunks[kSlots])() = { cm_thunk0, cm_thunk1, cm_thunk2, cm_thunk3, cm_thunk4 };

    Settings g_set;
    std::wstring g_dir;
    std::atomic<bool> g_stop{false};
    std::atomic<bool> g_hooked[kSlots];
    std::atomic<long> g_calls[kSlots];
    HANDLE g_worker = nullptr, g_event = nullptr;

    // Climb begin/end events handed from the game thread to the worker.
    struct Ev { uintptr_t self; int kind; DWORD tick; };
    constexpr unsigned kRing = 64;
    Ev g_ring[kRing];
    std::atomic<unsigned> g_head{0}, g_tail{0};
    std::atomic<long> g_dropped{0};
    std::atomic<long> g_climbDepth{0};

    void Post(uintptr_t self, int kind)
    {
        unsigned h = g_head.load(std::memory_order_relaxed);
        if (h - g_tail.load(std::memory_order_acquire) >= kRing) { g_dropped++; return; }
        g_ring[h % kRing] = { self, kind, GetTickCount() };
        g_head.store(h + 1, std::memory_order_release);
        if (g_event) SetEvent(g_event);
    }

    std::wstring IniPath() { return g_dir + L"ClimbmodASI.ini"; }
    int IniInt(const wchar_t* key, int def)
    {
        return (int)GetPrivateProfileIntW(L"climbmod", key, def, IniPath().c_str());
    }
    void ReadSettings()
    {
        g_set.enabled        = IniInt(L"Enabled", 1) != 0;
        g_set.logLevel       = IniInt(L"LogLevel", 1);
        g_set.hookPointClimb = IniInt(L"HookPointClimb", 1) != 0;
        g_set.equipScan      = IniInt(L"Probe", 1) != 0;
        g_set.maxScans       = IniInt(L"MaxProbes", 8);
        g_set.nodeBudget     = IniInt(L"ScanNodeBudget", 600);
        g_set.animSwap       = IniInt(L"AnimSwap", 0) != 0;
        g_set.keepAxesDrawn  = IniInt(L"KeepAxesDrawn", 0) != 0;
        if (g_set.maxScans < 0) g_set.maxScans = 0;
        if (g_set.maxScans > 64) g_set.maxScans = 64;
        if (g_set.nodeBudget < 50) g_set.nodeBudget = 50;
        if (g_set.nodeBudget > 4000) g_set.nodeBudget = 4000;
    }

    // ---- fail-closed identity gate -----------------------------------------
    bool IdentityOk(char* why, size_t n)
    {
        const auto& g = mem::Game();
        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g.base);
        auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g.base + dos->e_lfanew);
        for (const BuildDef* b : kBuilds)
            if (nt->FileHeader.TimeDateStamp == b->timeDateStamp && nt->OptionalHeader.SizeOfImage == b->sizeOfImage &&
                nt->OptionalHeader.CheckSum == b->checkSum) { g_b = b; return true; }
        snprintf(why, n, "PE identity TimeDateStamp=%08lX SizeOfImage=%08lX CheckSum=%08lX matches neither "
                 "exe 1.0.0.3663 (%08X/%08X/%08X) nor 1.0.0.3576 (%08X/%08X/%08X)",
                 (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)nt->OptionalHeader.SizeOfImage,
                 (unsigned long)nt->OptionalHeader.CheckSum, k3663.timeDateStamp, k3663.sizeOfImage, k3663.checkSum,
                 k3576.timeDateStamp, k3576.sizeOfImage, k3576.checkSum);
        return false;
    }
    bool VtableOk(const Vt& v)
    {
        const char* name = mem::RttiOfVtable(mem::Game().base + v.rva);
        return name && strcmp(name, v.rtti) == 0;
    }

    // ---- hooks ---------------------------------------------------------------
    uintptr_t SlotAddr(unsigned i) { return mem::Game().base + g_b->pointClimb.rva + 8u * kSlotIndex[i]; }

    int InstallHooks()
    {
        int ok = 0;
        for (unsigned i = 0; i < kSlots; ++i)
        {
            const uintptr_t expect = mem::Game().base + g_b->slotRva[i];
            cm_orig[i] = expect;
            if (!mem::SwapPointer(SlotAddr(i), expect, reinterpret_cast<uintptr_t>(kThunks[i])))
            {
                uintptr_t cur = 0; mem::Get(SlotAddr(i), cur);
                LOGI("[hook] PointClimb slot %u (%s) NOT hooked: holds %p, expected game+0x%X. Another mod "
                     "may own it; leaving it alone.", kSlotIndex[i], kSlotLabel[i],
                     (void*)cur, g_b->slotRva[i]);
                continue;
            }
            g_hooked[i] = true; ++ok;
            LOGV("[hook] PointClimb slot %u (%s) -> thunk %u", kSlotIndex[i], kSlotLabel[i], i);
        }
        return ok;
    }
    void RemoveHooks()
    {
        for (unsigned i = 0; i < kSlots; ++i)
            if (g_hooked[i].exchange(false))
                mem::SwapPointer(SlotAddr(i), reinterpret_cast<uintptr_t>(kThunks[i]), cm_orig[i]);
    }
}
}

// Game thread. Must stay tiny and never fail: count, and hand begin/end to the worker.
extern "C" void cm_on_slot(unsigned idx, uintptr_t self)
{
    using namespace cm;
    if (idx >= kSlots) return;
    g_calls[idx].fetch_add(1, std::memory_order_relaxed);
    const unsigned slot = cm::build::kSlotIndex[idx];
    if (slot == 7) { g_climbDepth++; Post(self, 7); }
    else if (slot == 8) { g_climbDepth--; Post(self, 8); }
}

namespace cm
{
namespace
{
    // ======================================================================
    // 0.1.3-diag probes. Worker thread only; every game read goes through
    // mem::Read (VirtualQuery + ReadProcessMemory), so a stale pointer fails the
    // read instead of faulting. Nothing is written to the game.
    //
    //  [inv]   deep walk of ClientInventoryActorComponent: every item-key-like
    //          dword (1000000..2099999) with count + sample path, climbmod keys
    //          1990000/1/2 with context, and what changed since the last session
    //          (so carrying different gear between climbs shows up directly).
    //  [equip] item-key-like dwords in the equip component (in-hand item).
    //  [state] subtree snapshots of ctrl/move/stat on the wall (x2) and off it
    //          (x2): small-int fields stable on the wall that differ off it
    //          (anim node / move mode / stow), and values that fall while on
    //          the wall and rise again off it (stamina candidates).
    // ======================================================================
    bool IsKey(uint32_t v) { return v == kItemAxeAdze || v == kItemAxeHammer || v == kItemBoots; }
    bool KeyLike(uint32_t v) { return v >= 1000000u && v < 2100000u; }
    std::string Short(const char* r) { return r ? std::string(r + 4, strcspn(r + 4, "@")) : std::string("?"); }

    size_t ReadUpTo(uintptr_t a, uint8_t* out, size_t want)
    {
        for (size_t n = want; n >= 0x40; n /= 2) if (mem::Read(a, out, n)) return n;
        return 0;
    }

    struct Blk { std::string chain; uintptr_t addr; std::vector<uint8_t> d; };

    // Bounded BFS below root over every plausible heap pointer (typed or raw).
    std::vector<Blk> Walk(uintptr_t root, const char* rootName, int maxDepth, size_t maxBlocks, size_t blockSize, size_t rootSize)
    {
        std::vector<Blk> out;
        if (!root) return out;
        struct N { uintptr_t a; int d; std::string chain; };
        std::vector<N> q{ { root, 0, rootName } };
        std::unordered_set<uintptr_t> seen{ root };
        std::vector<uint8_t> buf(std::max(blockSize, rootSize));
        for (size_t qi = 0; qi < q.size() && out.size() < maxBlocks && !g_stop; ++qi)
        {
            const N nd = q[qi];
            const size_t n = ReadUpTo(nd.a, buf.data(), nd.d == 0 ? rootSize : blockSize);
            if (!n) continue;
            out.push_back({ nd.chain, nd.a, std::vector<uint8_t>(buf.begin(), buf.begin() + n) });
            if (nd.d >= maxDepth) continue;
            for (size_t o = 0; o + 8 <= n; o += 8)
            {
                uintptr_t p; memcpy(&p, buf.data() + o, 8);
                if (!mem::PlausibleHeap(p) || mem::InImage(p) || !seen.insert(p).second) continue;
                const char* r = mem::RttiOfObject(p);
                if (r && (strstr(r, "UIGamePlay") || strstr(r, "hk") || strstr(r, "Actor@") || strstr(r, "Lock") ||
                          strstr(r, "SceneObject") || strstr(r, "ActorComponent@"))) continue; // stay inside this component
                char t[24]; snprintf(t, sizeof t, "+0x%zX>", o);
                q.push_back({ p, nd.d + 1, nd.chain + t + (r ? Short(r) : std::string("[raw]")) });
            }
        }
        return out;
    }

    struct Comps { uintptr_t actor = 0, equip = 0, inv = 0, ctrl = 0; std::string invPath, equipPath; uintptr_t stat[3] = {}; int nStat = 0; };

    Comps FindComps(uintptr_t self, bool logList)
    {
        Comps c;
        mem::Get(self + g_b->ownerOffset, c.ctrl);
        mem::Get(self + 0x20, c.actor);
        uintptr_t arr = 0; uint32_t cnt = 0;
        mem::Get(c.actor + 0x68, arr); mem::Get(c.actor + 0x70, cnt);
        if (logList) LOGI("[probe] actor=[climb+0x20]=%p (%s) components=[actor+0x68]=%p count?=[actor+0x70]=%u",
                          (void*)c.actor, Short(mem::RttiOfObject(c.actor)).c_str(), (void*)arr, cnt);
        for (unsigned i = 0; i < 96; ++i)
        {
            uintptr_t p = 0; if (!mem::Get(arr + 8u * i, p)) break;
            const char* r = mem::RttiOfObject(p);
            if (!r) { if (i >= cnt && cnt) break; continue; }
            if (logList) LOGI("[probe]   comp[%u] actor+0x68>[+0x%X] = %s", i, 8u * i, Short(r).c_str());
            char path[64]; snprintf(path, sizeof path, "climb+0x20>actor+0x68>[+0x%X]", 8u * i);
            if (!c.equip && strstr(r, "EquipSlotActorComponent")) { c.equip = p; c.equipPath = path; }
            if (!c.inv && strstr(r, "InventoryActorComponent")) { c.inv = p; c.invPath = path; }
            if (c.nStat < 3 && (strstr(r, "Stat") || strstr(r, "Status")) && strstr(r, "Component")) c.stat[c.nStat++] = p;
        }
        if (!c.inv)
        {
            // Bounded typed walk from actor and climb object for an inventory component.
            struct N { uintptr_t a; int d; std::string path; };
            std::vector<N> q{ { c.actor, 0, "climb+0x20" }, { self, 0, "climb" } };
            std::vector<uintptr_t> seen{ c.actor, self };
            static uint8_t buf[0x800];
            for (size_t qi = 0; qi < q.size() && q.size() < 2500 && !c.inv; ++qi)
            {
                const N nd = q[qi]; const size_t n = ReadUpTo(nd.a, buf, 0x800); if (!n) continue;
                for (size_t o = 8; o + 8 <= n; o += 8)
                {
                    uintptr_t p; memcpy(&p, buf + o, 8);
                    if (!mem::PlausibleHeap(p) || mem::InImage(p)) continue;
                    bool dup = false; for (auto s : seen) if (s == p) { dup = true; break; } if (dup) continue; seen.push_back(p);
                    const char* r = mem::RttiOfObject(p); if (!r) continue;
                    char t[24]; snprintf(t, sizeof t, "+0x%zX>", o);
                    std::string path = nd.path + t + Short(r);
                    if (strstr(r, "InventoryActorComponent")) { c.inv = p; c.invPath = path; break; }
                    if (nd.d < 4 && !strstr(r, "UIGamePlay")) q.push_back({ p, nd.d + 1, path });
                }
            }
        }
        return c;
    }

    // ---- [census] heap-wide count of the climbmod item keys ------------------------
    // ItemInfo rows for 1990000/1/2 live in loaded game data whether or not the player
    // carries them; inventory entries are extra copies. So the per-key hit count and
    // which addresses appear/disappear between climbs (with different gear carried)
    // is a direct, walk-independent presence signal. Read-only, worker thread.
    struct Census { std::vector<uintptr_t> hits[3]; double mb = 0; DWORD ms = 0; bool truncated = false; };
    Census g_prevCensus; bool g_haveCensus = false;

    template <class F> void ForEachRwRegion(F&& f, DWORD budgetMs, bool& truncated)
    {
        const auto& g = mem::Game();
        // Own VirtualAlloc region, so skipping "our" memory skips exactly this buffer.
        struct Buf { uint8_t* p; size_t n; uint8_t* data() { return p; } size_t size() const { return n; } };
        static Buf buf{ (uint8_t*)VirtualAlloc(nullptr, 1u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), 1u << 20 };
        if (!buf.p) { truncated = true; return; }
        const DWORD t0 = GetTickCount();
        uintptr_t a = 0x10000;
        MEMORY_BASIC_INFORMATION mbi;
        while (a < 0x00007FFFFFFF0000ull && VirtualQuery((void*)a, &mbi, sizeof mbi) && !g_stop)
        {
            const uintptr_t base = (uintptr_t)mbi.BaseAddress, end = base + mbi.RegionSize;
            const DWORD prot = mbi.Protect & 0xFF;
            const bool rw = prot == PAGE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READWRITE;
            const bool inImage = base >= g.base && base < g.base + g.size;
            // Never count our own scratch: the read buffer and this thread's stack.
            const uintptr_t me = (uintptr_t)&mbi, bp = (uintptr_t)buf.data();
            const bool ours = (me >= base && me < end) || (bp + buf.size() > base && bp < end);
            const bool slow = (mbi.Protect & (PAGE_NOCACHE | PAGE_WRITECOMBINE)) != 0; // GPU/uncached mappings
            if (mbi.State == MEM_COMMIT && rw && !(mbi.Protect & PAGE_GUARD) && !inImage && !ours && !slow)
                for (uintptr_t p = base; p < end; p += buf.size())
                {
                    const size_t n = (size_t)std::min<uintptr_t>(buf.size(), end - p);
                    if (mem::Read(p, buf.data(), n)) f(p, buf.data(), n);
                    if (GetTickCount() - t0 > budgetMs) { truncated = true; return; }
                }
            a = end;
        }
    }

    Census TakeCensus()
    {
        Census c; const DWORD t0 = GetTickCount();
        ForEachRwRegion([&](uintptr_t p, const uint8_t* d, size_t n) {
            c.mb += n / 1048576.0;
            for (size_t o = 0; o + 4 <= n; o += 4)
            {
                uint32_t v; memcpy(&v, d + o, 4);
                if (v - kItemAxeAdze <= 2u) { auto& h = c.hits[v - kItemAxeAdze]; if (h.size() < 512) h.push_back(p + o); }
            }
        }, 25000, c.truncated);
        c.ms = GetTickCount() - t0;
        return c;
    }

    struct Range { uintptr_t lo, hi; std::string chain; };
    std::vector<Range> g_invRanges; // blocks of the narrowed inventory walk (latest probe)

    const Range* InInv(uintptr_t a)
    {
        for (auto& r : g_invRanges) if (a >= r.lo && a < r.hi) return &r;
        return nullptr;
    }

    // Who points at (or up to 0x100 before) each key copy? If the inventory stores a
    // pointer to the ItemInfo row instead of the key, the number of owners of the row
    // changes with every pickup/drop even though the key copies themselves do not.
    using Owners = std::map<uintptr_t, std::vector<uintptr_t>>; // target -> pointer locations
    Owners g_prevOwners; bool g_haveOwners = false;

    Owners OwnerSearch(const std::vector<std::pair<uintptr_t, uint32_t>>& targets, bool& trunc)
    {
        Owners res;
        if (targets.empty()) return res;
        uintptr_t lo = ~(uintptr_t)0, hi = 0;
        for (auto& t : targets) { lo = std::min(lo, t.first - 0x100); hi = std::max(hi, t.first); }
        ForEachRwRegion([&](uintptr_t p, const uint8_t* d, size_t n) {
            for (size_t o = 0; o + 8 <= n; o += 8)
            {
                uintptr_t q; memcpy(&q, d + o, 8);
                if (q < lo || q >= hi || (q & 7)) continue;
                for (auto& t : targets)
                    if (q < t.first && t.first - q <= 0x100) { auto& v = res[t.first]; if (v.size() < 256) v.push_back(p + o); break; }
            }
        }, 25000, trunc);
        return res;
    }

    void LogOwners(int sess, const std::vector<std::pair<uintptr_t, uint32_t>>& targets)
    {
        bool trunc = false;
        Owners o = OwnerSearch(targets, trunc);
        for (auto& t : targets)
        {
            const auto& cur = o[t.first];
            int add = 0, rem = 0;
            if (g_haveOwners)
            {
                const auto& old = g_prevOwners[t.first];
                for (auto a : cur) if (std::find(old.begin(), old.end(), a) == old.end()) ++add;
                for (auto a : old) if (std::find(cur.begin(), cur.end(), a) == cur.end()) ++rem;
            }
            LOGI("[owners %d] key %u copy %p: %zu pointer(s) to it%s (+%d new, -%d gone since previous climb)%s", sess, t.second, (void*)t.first,
                 cur.size(), cur.size() >= 256 ? "+" : "", add, rem, trunc ? " [scan truncated]" : "");
            int shown = 0;
            for (auto a : cur)
            {
                const bool isNew = g_haveOwners && std::find(g_prevOwners[t.first].begin(), g_prevOwners[t.first].end(), a) == g_prevOwners[t.first].end();
                if (g_haveOwners && !isNew) continue;
                if (shown++ >= 6) break;
                uintptr_t q = 0; mem::Get(a, q);
                const Range* r = InInv(a);
                uint32_t ctx[8] = {}; mem::Read(a - 0x10, ctx, sizeof ctx);
                LOGI("[owners %d]   %s[%p] -> +0x%zX  %s%s  ctx %08X %08X %08X %08X | %08X %08X %08X %08X", sess, isNew ? "NEW " : "", (void*)a,
                     (size_t)(t.first - q), r ? "in inv walk " : "", r ? r->chain.c_str() : "", ctx[0], ctx[1], ctx[2], ctx[3], ctx[4], ctx[5], ctx[6], ctx[7]);
            }
        }
        g_prevOwners = std::move(o); g_haveOwners = true;
    }

    void CensusProbe(int sess)
    {
        Census c = TakeCensus();
        const char* nm[3] = { "AxeAdze(1990000)", "AxeHammer(1990001)", "Crampons(1990002)" };
        LOGI("[census %d] scanned %.0f MB of writable memory in %lu ms%s: %s x%zu, %s x%zu, %s x%zu", sess, c.mb, (unsigned long)c.ms,
             c.truncated ? " (TRUNCATED at 25 s)" : "", nm[0], c.hits[0].size(), nm[1], c.hits[1].size(), nm[2], c.hits[2].size());
        std::vector<std::pair<uintptr_t, uint32_t>> fresh;
        for (int k = 0; k < 3; ++k)
        {
            const auto& cur = c.hits[k];
            if (g_haveCensus)
            {
                const auto& old = g_prevCensus.hits[k];
                int add = 0, rem = 0;
                for (auto a : cur) if (std::find(old.begin(), old.end(), a) == old.end()) { ++add; if (fresh.size() < 6) fresh.push_back({ a, kItemAxeAdze + k }); }
                for (auto a : old) if (std::find(cur.begin(), cur.end(), a) == cur.end()) ++rem;
                LOGI("[census %d] %s: %d new copies, %d gone since the previous climb", sess, nm[k], add, rem);
            }
            else for (auto a : cur) if (fresh.size() < 6) fresh.push_back({ a, kItemAxeAdze + k });
            int shown = 0;
            for (auto a : cur)
            {
                if (shown++ >= 8) break;
                uint32_t ctx[12] = {}; mem::Read(a - 0x10, ctx, sizeof ctx);
                const Range* r = InInv(a);
                LOGI("[census %d]   %u @%p%s%s: %08X %08X %08X %08X [%08X] %08X %08X %08X %08X %08X %08X %08X", sess, kItemAxeAdze + k, (void*)a,
                     r ? " in inv walk " : "", r ? r->chain.c_str() : "", ctx[0], ctx[1], ctx[2], ctx[3], ctx[4], ctx[5], ctx[6], ctx[7], ctx[8], ctx[9], ctx[10], ctx[11]);
            }
        }
        std::vector<std::pair<uintptr_t, uint32_t>> targets;
        for (int k = 0; k < 3; ++k) for (size_t i = 0; i < c.hits[k].size() && i < 4; ++i) targets.push_back({ c.hits[k][i], kItemAxeAdze + k });
        LogOwners(sess, targets);
        for (auto& f : fresh) LOGI("[census %d] new copy of %u at %p", sess, f.second, (void*)f.first);
        g_prevCensus = std::move(c); g_haveCensus = true;
    }

    // ---- [inv] narrowed walk ----------------------------------------------------
    // Only the component's own container fields (from the 0.1.4 layout dump), and only
    // typed objects whose class name mentions Item/Inventory: the 0.1.4 walk leaked into
    // faction managers, frame events and neighbouring heap objects.
    void InventoryProbe(const Comps& c, int sess)
    {
        g_invRanges.clear();
        if (!c.inv) { LOGI("[inv %d] no ClientInventoryActorComponent on the climbing actor", sess); return; }
        static const uint32_t roots[] = { 0x18, 0x48, 0x68, 0x70, 0xF8, 0x100, 0x118, 0x140 };
        std::string line;
        for (uint32_t ro : roots)
        {
            uintptr_t p = 0; uint32_t cnt = 0; mem::Get(c.inv + ro, p); mem::Get(c.inv + ro + 8, cnt);
            if (!mem::PlausibleHeap(p)) continue;
            struct N { uintptr_t a; int d; std::string chain; };
            char rn[24]; snprintf(rn, sizeof rn, "inv+0x%X", ro);
            std::vector<N> q{ { p, 1, std::string(rn) } };
            std::unordered_set<uintptr_t> seen{ p };
            int keyLike = 0, ours = 0; static uint8_t buf[0x200];
            for (size_t qi = 0; qi < q.size() && q.size() < 1500; ++qi)
            {
                const N nd = q[qi]; const size_t n = ReadUpTo(nd.a, buf, sizeof buf); if (!n) continue;
                g_invRanges.push_back({ nd.a, nd.a + n, nd.chain });
                for (size_t o = 0; o + 4 <= n; o += 4)
                {
                    uint32_t v; memcpy(&v, buf + o, 4);
                    if (KeyLike(v)) ++keyLike;
                    if (IsKey(v)) { ++ours; LOGI("[inv %d] climbmod key %u at %s+0x%zX", sess, v, nd.chain.c_str(), o); }
                }
                if (nd.d >= 4) continue;
                for (size_t o = 0; o + 8 <= n; o += 8)
                {
                    uintptr_t x; memcpy(&x, buf + o, 8);
                    if (!mem::PlausibleHeap(x) || mem::InImage(x) || !seen.insert(x).second) continue;
                    const char* r = mem::RttiOfObject(x);
                    if (r && !strstr(r, "Item") && !strstr(r, "Inventory")) continue;
                    char t[24]; snprintf(t, sizeof t, "+0x%zX>", o);
                    q.push_back({ x, nd.d + 1, nd.chain + t + (r ? Short(r) : std::string("[raw]")) });
                }
            }
            char s[96]; snprintf(s, sizeof s, " %s(n=%u):%zu blk/%d keys/%d ours", rn, cnt, q.size(), keyLike, ours); line += s;
        }
        LOGI("[inv %d] narrowed walk:%s", sess, line.c_str());
    }

    // ---- [track] 1 Hz candidates for stamina and climb state ----------------------
    // 0.1.4 session 2: [[stat+0x90]+0x30]+0xD8 (same object as [[ctrl+0x20]+0x110]+0xD8)
    // fell 15162->14666 on the wall and rose 12902->14506 off it: stamina-like.
    void Track(const Comps& c, bool onWall)
    {
        if (!c.ctrl || !c.nStat) return;
        uintptr_t s1 = 0, s2 = 0, a1 = 0, a2 = 0, move = 0;
        mem::Get(c.stat[0] + 0x90, s1); mem::Get(s1 + 0x30, s2);
        mem::Get(c.ctrl + 0x20, a1); mem::Get(a1 + 0x110, a2);
        mem::Get(c.ctrl + 0x140, move);
        float f[6] = {}; mem::Read(s2 + 0xD0, f, sizeof f);
        uint32_t mode = 0, ctl164 = 0; uint8_t flag477 = 0;
        mem::Get(move + 0x10, mode); mem::Get(c.ctrl + 0x164, ctl164); mem::Get(c.stat[0] + 0x477, flag477);
        static float last = -1; static uint32_t lastMode = 0xFFFFFFFF;
        if (f[2] == last && mode == lastMode && !onWall) return;
        last = f[2]; lastMode = mode;
        LOGI("[track] %s S=%p%s +0xD0..E4: %g %g [%g] %g %g %g | move.mode=%u ctrl+0x164=%u stat+0x477=%u",
             onWall ? "WALL" : "off ", (void*)s2, s2 == a2 ? "" : " (ctrl path differs)", f[0], f[1], f[2], f[3], f[4], f[5], mode, ctl164, flag477);
    }

    // ---- worker --------------------------------------------------------------
    DWORD WINAPI Worker(LPVOID)
    {
        constexpr unsigned kU2 = 1; // index of update2 in kSlotLabel
        DWORD lastSummary = GetTickCount(), lastU2Change = 0, sessStart = 0, endedAt = 0, lastTrack = 0;
        long prev[kSlots] = {}, lastU2 = 0;
        bool climbing = false, probedThis = false, haveComps = false; int sess = 0, grabs = 0, probed = 0;
        uintptr_t self = 0;
        Comps comps;
        while (!g_stop)
        {
            WaitForSingleObject(g_event, 100);
            const DWORD now = GetTickCount();
            const long u2 = g_calls[kU2].load();
            if (u2 != lastU2) { lastU2 = u2; lastU2Change = now; }
            while (g_tail.load(std::memory_order_acquire) != g_head.load(std::memory_order_acquire))
            {
                unsigned t = g_tail.load(std::memory_order_relaxed);
                Ev e = g_ring[t % kRing];
                g_tail.store(t + 1, std::memory_order_release);
                if (e.kind == 7)
                {
                    self = e.self; ++grabs; lastU2Change = now;
                    if (!climbing)
                    {
                        climbing = true; ++sess; grabs = 1; sessStart = e.tick; probedThis = false;
                        LOGI("[climb] session %d: PointClimb begin this=%p", sess, (void*)e.self);
                    }
                    else LOGV("[climb] grab/begin #%d this=%p", grabs, (void*)e.self);
                }
                else LOGV("[climb] PointClimb end this=%p", (void*)e.self);
            }
            if (climbing && !probedThis && now - sessStart >= 600)
            {
                probedThis = true;
                comps = FindComps(self, !haveComps); haveComps = true;
                if (g_set.equipScan && probed < g_set.maxScans)
                {
                    ++probed;
                    InventoryProbe(comps, sess);
                    CensusProbe(sess);
                }
            }
            if (climbing && now - lastU2Change > 1500)
            {
                climbing = false; endedAt = now;
                LOGI("[climb] session %d ended: %lu ms on the wall, %d grab(s)", sess, (unsigned long)(lastU2Change - sessStart), grabs);
            }
            // 1 Hz tracking while on the wall and for 8 s after leaving it.
            if (haveComps && now - lastTrack >= 1000 && (climbing || now - endedAt < 8000))
            {
                lastTrack = now;
                Track(comps, climbing);
            }
            if (now - lastSummary >= 10000)
            {
                lastSummary = now;
                bool any = false; char line[256]; int p = 0;
                for (unsigned i = 0; i < kSlots; ++i)
                {
                    long c = g_calls[i].load(); if (c != prev[i]) any = true;
                    p += snprintf(line + p, sizeof line - p, " %s=%ld", kSlotLabel[i], c - prev[i]);
                    prev[i] = c;
                }
                if (any) LOGV("[calls/10s]%s dropped=%ld", line, g_dropped.load());
            }
        }
        return 0;
    }
}

void Start(HMODULE self)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(self, path, MAX_PATH);
    g_dir = path; g_dir.resize(g_dir.find_last_of(L"\\/") + 1);

    // Only the game itself. crashpad_handler.exe loads ASIs too.
    wchar_t exe[MAX_PATH] = {}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* leaf = wcsrchr(exe, L'\\'); leaf = leaf ? leaf + 1 : exe;
    if (_wcsicmp(leaf, L"CrimsonDesert.exe") != 0) return;

    ReadSettings();
    log::Open((g_dir + L"ClimbmodASI.log").c_str(), g_set.logLevel);
    LOGI("[mod] Climbmod ASI %s (accepts exe 1.0.0.3663 and 1.0.0.3576 only). LogLevel=%d Enabled=%d HookPointClimb=%d "
         "Probe=%d MaxProbes=%d AnimSwap=%d KeepAxesDrawn=%d", kVersion, g_set.logLevel, g_set.enabled, g_set.hookPointClimb,
         g_set.equipScan, g_set.maxScans, g_set.animSwap, g_set.keepAxesDrawn);
    {
        // DMM may leave an older INI next to a newer ASI (or the reverse); say so loudly.
        char head[160] = {}; FILE* f = _wfopen(IniPath().c_str(), L"rb");
        if (f) { fread(head, 1, sizeof head - 1, f); fclose(f); }
        if (!strstr(head, kVersion))
            LOGI("[mod] WARNING: ClimbmodASI.ini next to this ASI is not the %s INI (stale deploy?). Defaults are used for missing keys.", kVersion);
    }
    if (!g_set.enabled) { LOGI("[mod] Enabled=0, doing nothing."); return; }

    char why[256];
    if (!IdentityOk(why, sizeof why)) { LOGI("[mod] unknown game build, doing nothing: %s", why); return; }
    LOGI("[mod] game build recognised: %s", g_b->label);
    const Vt checks[] = { g_b->pointClimb, g_b->ladderClimb, g_b->clientEquipSlot, g_b->commonEquipSlot, g_b->serverEquipSlot, g_b->clientSkill };
    for (const auto& v : checks)
        if (!VtableOk(v)) { LOGI("[mod] RTTI check failed for %s at game+0x%X, doing nothing.", v.rtti, v.rva); return; }
    LOGI("[mod] build identity and 6 RTTI vtables verified.");

    if (g_set.animSwap || g_set.keepAxesDrawn)
        LOGI("[mod] AnimSwap/KeepAxesDrawn requested but this is the diagnostic build: the animation selector "
             "and hand-prop attach are not proven yet, so neither is applied. Vanilla animation is unchanged.");

    if (!g_set.hookPointClimb) { LOGI("[mod] HookPointClimb=0, nothing hooked."); return; }
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    const int n = InstallHooks();
    LOGI("[mod] %d of %u PointClimbMovementMethod vtable slots observed (log-only, original always called).", n, kSlots);
}

void Stop(bool processExit)
{
    g_stop = true;
    if (!processExit)
    {
        RemoveHooks();
        if (g_event) SetEvent(g_event);
        if (g_worker) { WaitForSingleObject(g_worker, 2000); CloseHandle(g_worker); g_worker = nullptr; }
    }
    LOGI("[mod] stopping.");
    log::Close();
}
}
