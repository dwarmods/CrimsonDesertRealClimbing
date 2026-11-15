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

    // ---- [inv] / [equip] --------------------------------------------------------
    struct KeyInfo { int count = 0; std::string chain; };
    std::map<uint32_t, KeyInfo> g_prevInv; bool g_havePrevInv = false;
    uint32_t g_inHand = 0;

    std::map<uint32_t, KeyInfo> KeyScan(const std::vector<Blk>& blks)
    {
        std::map<uint32_t, KeyInfo> m;
        for (auto& b : blks)
            for (size_t o = 0; o + 4 <= b.d.size(); o += 4)
            {
                uint32_t v; memcpy(&v, &b.d[o], 4);
                if (!KeyLike(v)) continue;
                auto& k = m[v];
                if (!k.count++) { char t[24]; snprintf(t, sizeof t, "+0x%zX", o); k.chain = b.chain + t; }
            }
        return m;
    }

    void InventoryProbe(const Comps& c, int sess)
    {
        if (!c.inv) { LOGI("[inv %d] no ClientInventoryActorComponent on the climbing actor", sess); return; }
        static bool layoutDone = false;
        if (!layoutDone)
        {
            // One-time layout of the component: typed pointers, raw pointers (+ the dword after
            // them, often an array count) and small ints, so the item list can be found by hand
            // if the walk below misses it.
            layoutDone = true;
            uint8_t hdr[0x200] = {};
            const size_t n = ReadUpTo(c.inv, hdr, sizeof hdr);
            for (size_t o = 8; o + 8 <= n; o += 8)
            {
                uintptr_t p; memcpy(&p, hdr + o, 8);
                uint32_t lo = (uint32_t)p, hi = (uint32_t)(p >> 32);
                if (mem::PlausibleHeap(p) && !mem::InImage(p))
                    LOGI("[inv layout] +0x%03zX ptr %p %s next=%u", o, (void*)p, Short(mem::RttiOfObject(p)).c_str(), o + 12 <= n ? *(uint32_t*)(hdr + o + 8) : 0);
                else if (p && (lo <= 100000 || hi <= 100000) && !mem::InImage(p))
                    LOGI("[inv layout] +0x%03zX ints %u %u", o, lo, hi);
            }
        }
        auto blks = Walk(c.inv, "inv", 6, 6000, 0x200, 0x1000);
        auto keys = KeyScan(blks);
        LOGI("[inv %d] %s at %s: walked %zu blocks, %zu distinct item-key-like values (1000000..2099999)",
             sess, Short(mem::RttiOfObject(c.inv)).c_str(), c.invPath.c_str(), blks.size(), keys.size());
        const uint32_t ck[3] = { kItemAxeAdze, kItemAxeHammer, kItemBoots };
        const char* cn[3] = { "AxeAdze", "AxeHammer", "Crampons" };
        for (int i = 0; i < 3; ++i)
        {
            auto it = keys.find(ck[i]);
            if (it == keys.end()) { LOGI("[inv %d] %s(%u): not found", sess, cn[i], ck[i]); continue; }
            LOGI("[inv %d] %s(%u): PRESENT x%d, first at %s", sess, cn[i], ck[i], it->second.count, it->second.chain.c_str());
        }
        // Context of the first climbmod hit (count/slot fields sit next to the key).
        for (auto& b : blks)
            for (size_t o = 0; o + 4 <= b.d.size(); o += 4)
            {
                uint32_t v; memcpy(&v, &b.d[o], 4);
                if (!IsKey(v)) continue;
                uint32_t ctx[16] = {}; mem::Read(b.addr + o - 0x20, ctx, sizeof ctx);
                LOGI("[inv %d] ctx %u @%s+0x%zX: %08X %08X %08X %08X %08X %08X %08X %08X | %08X %08X %08X %08X %08X %08X %08X %08X",
                     sess, v, b.chain.c_str(), o, ctx[0], ctx[1], ctx[2], ctx[3], ctx[4], ctx[5], ctx[6], ctx[7], ctx[8], ctx[9], ctx[10], ctx[11], ctx[12], ctx[13], ctx[14], ctx[15]);
                goto ctxdone;
            }
    ctxdone:
        if (g_inHand) LOGI("[inv %d] control: in-hand item %u from equip is %s in the inventory walk", sess, g_inHand, keys.count(g_inHand) ? "PRESENT" : "absent");
        if (!g_havePrevInv)
        {
            int k = 0;
            for (auto& kv : keys) { if (k++ >= 40) break; LOGI("[inv %d]   key %u x%d at %s", sess, kv.first, kv.second.count, kv.second.chain.c_str()); }
        }
        else
        {
            int add = 0, rem = 0;
            for (auto& kv : keys) if (!g_prevInv.count(kv.first) && add++ < 20) LOGI("[inv %d]   +added   %u x%d at %s", sess, kv.first, kv.second.count, kv.second.chain.c_str());
            for (auto& kv : g_prevInv) if (!keys.count(kv.first) && rem++ < 20) LOGI("[inv %d]   -removed %u (was at %s)", sess, kv.first, kv.second.chain.c_str());
            LOGI("[inv %d] since last probe: %d key(s) added, %d removed", sess, add, rem);
        }
        g_prevInv = keys; g_havePrevInv = true;
    }

    void EquipProbe(const Comps& c, int sess, const char* when)
    {
        if (!c.equip) return;
        uint32_t hand = 0; mem::Get(c.equip + 0x40, hand);
        if (KeyLike(hand)) g_inHand = hand;
        auto keys = KeyScan(Walk(c.equip, "equip", 2, 300, 0x200, 0x1000));
        std::string line; int k = 0;
        for (auto& kv : keys) { if (k++ >= 24) break; char t[160]; snprintf(t, sizeof t, " %u@%s", kv.first, kv.second.chain.c_str()); line += t; }
        LOGI("[equip %d %s] [equip+0x40]=%d; %zu key-like value(s):%s", sess, when, (int)hand, keys.size(), line.c_str());
    }

    // ---- [state] -----------------------------------------------------------------
    using Snap = std::vector<Blk>;
    struct SnapSet { Snap ctrl, move, stat, climb; };

    SnapSet TakeSnaps(uintptr_t self, const Comps& c)
    {
        SnapSet s;
        uintptr_t move = 0; mem::Get(c.ctrl + 0x140, move);
        s.climb = Walk(self, "climb", 0, 1, 0x400, 0x400);
        s.ctrl = Walk(c.ctrl, "ctrl", 2, 220, 0x200, 0x800);
        s.move = Walk(move, "move", 1, 60, 0x200, 0x800);
        s.stat = Walk(c.nStat ? c.stat[0] : 0, "stat", 2, 220, 0x200, 0x800);
        return s;
    }

    const Blk* Match(const Snap& s, const Blk& b)
    {
        for (auto& x : s) if (x.addr == b.addr && x.chain == b.chain) return &x;
        return nullptr;
    }

    bool SaneF(float f) { return f == f && f > 0.01f && f < 100000.f; }

    void DiffState(int sess, const char* label, const Snap& h1, const Snap& h2, const Snap& o1, const Snap* o2, int& flags, int& drains)
    {
        for (auto& a : h1)
        {
            const Blk* b = Match(h2, a); const Blk* o = Match(o1, a); const Blk* p = o2 ? Match(*o2, a) : nullptr;
            if (!b || !o) continue;
            const size_t n = std::min(std::min(a.d.size(), b->d.size()), o->d.size());
            for (size_t off = 0; off + 4 <= n; off += 4)
            {
                uint32_t va, vb, vo; memcpy(&va, &a.d[off], 4); memcpy(&vb, &b->d[off], 4); memcpy(&vo, &o->d[off], 4);
                if (va == vb && va != vo && (va <= 4096 || vo <= 4096) && flags < 40)
                {
                    const bool star = va == 18 || va == 52 || va == 59 || va == 60;
                    LOGI("[state %d] %s%s %s+0x%zX  wall=%u  off=%u", sess, star ? "* " : "", label, a.chain.c_str(), off, va, vo);
                    ++flags;
                }
                if (p && off + 4 <= p->d.size() && drains < 24)
                {
                    uint32_t vp; memcpy(&vp, &p->d[off], 4);
                    float fa, fb, fo, fp; memcpy(&fa, &va, 4); memcpy(&fb, &vb, 4); memcpy(&fo, &vo, 4); memcpy(&fp, &vp, 4);
                    const bool fdrain = SaneF(fa) && SaneF(fb) && SaneF(fo) && SaneF(fp) && fb < fa && fp > fo;
                    const bool idrain = va < 10000000u && vb < va && vp > vo && vo < 10000000u;
                    if (fdrain || idrain)
                    {
                        if (fdrain) LOGI("[stamina? %d] %s %s+0x%zX float wall %g -> %g, off %g -> %g", sess, label, a.chain.c_str(), off, fa, fb, fo, fp);
                        else LOGI("[stamina? %d] %s %s+0x%zX int wall %u -> %u, off %u -> %u", sess, label, a.chain.c_str(), off, va, vb, vo, vp);
                        ++drains;
                    }
                }
            }
        }
    }

    void DiffAll(int sess, const SnapSet& h1, const SnapSet& h2, const SnapSet& o1, const SnapSet* o2)
    {
        int flags = 0, drains = 0;
        DiffState(sess, "move", h1.move, h2.move, o1.move, o2 ? &o2->move : nullptr, flags, drains);
        DiffState(sess, "ctrl", h1.ctrl, h2.ctrl, o1.ctrl, o2 ? &o2->ctrl : nullptr, flags, drains);
        DiffState(sess, "climb", h1.climb, h2.climb, o1.climb, o2 ? &o2->climb : nullptr, flags, drains);
        DiffState(sess, "stat", h1.stat, h2.stat, o1.stat, o2 ? &o2->stat : nullptr, flags, drains);
        LOGI("[state %d] %d wall-vs-off small-int field(s), %d stamina candidate(s)%s; blocks ctrl=%zu move=%zu stat=%zu",
             sess, flags, drains, o2 ? "" : " (no second off-wall sample: climbed again within 1.5 s)", h1.ctrl.size(), h1.move.size(), h1.stat.size());
    }

    // ---- worker --------------------------------------------------------------
    DWORD WINAPI Worker(LPVOID)
    {
        constexpr unsigned kU2 = 1; // index of update2 in kSlotLabel
        DWORD lastSummary = GetTickCount(), lastU2Change = 0, sessStart = 0, endedAt = 0;
        long prev[kSlots] = {}, lastU2 = 0;
        bool climbing = false; int sess = 0, grabs = 0, probed = 0;
        uintptr_t self = 0;
        Comps comps; SnapSet h1, h2, o1;
        enum { Idle, WantH1, WantH2, WantOff, WantOff2 } phase = Idle;
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
                        if (phase == WantOff2) { LOGI("[state %d] climbed again before the second off-wall sample", sess); DiffAll(sess, h1, h2, o1, nullptr); phase = Idle; }
                        climbing = true; ++sess; grabs = 1; sessStart = e.tick;
                        LOGI("[climb] session %d: PointClimb begin this=%p", sess, (void*)e.self);
                        if (g_set.equipScan && probed < g_set.maxScans && phase == Idle) phase = WantH1;
                    }
                    else LOGV("[climb] grab/begin #%d this=%p", grabs, (void*)e.self);
                }
                else LOGV("[climb] PointClimb end this=%p", (void*)e.self);
            }
            if (climbing && phase == WantH1 && now - sessStart >= 600)
            {
                ++probed;
                comps = FindComps(self, probed == 1);
                EquipProbe(comps, sess, "wall");
                InventoryProbe(comps, sess);
                h1 = TakeSnaps(self, comps);
                phase = WantH2;
            }
            else if (climbing && phase == WantH2 && now - sessStart >= 1400)
            {
                h2 = TakeSnaps(self, comps);
                phase = WantOff;
            }
            if (climbing && now - lastU2Change > 1500)
            {
                climbing = false; endedAt = now;
                LOGI("[climb] session %d ended: %lu ms on the wall, %d grab(s)", sess, (unsigned long)(lastU2Change - sessStart), grabs);
                if (phase == WantOff) { o1 = TakeSnaps(self, comps); EquipProbe(comps, sess, "off"); phase = WantOff2; }
                else { if (phase != Idle) LOGI("[climb] session %d too short for the wall-vs-off diff (need >1.4 s on the wall).", sess); phase = Idle; }
            }
            if (!climbing && phase == WantOff2 && now - endedAt >= 1500)
            {
                SnapSet o2 = TakeSnaps(self, comps);
                DiffAll(sess, h1, h2, o1, &o2);
                phase = Idle;
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
