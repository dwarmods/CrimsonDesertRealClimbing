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
        g_set.maxScans       = IniInt(L"MaxProbes", 6);
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
    // 0.1.2-diag probes. Everything below runs on our worker thread and only
    // READS game memory through mem::Read (VirtualQuery + ReadProcessMemory),
    // so a stale pointer fails the read instead of faulting. Nothing is written.
    //
    //  A  presence probe: find the inventory (and equip) actor components by
    //     RTTI name and deep-search them for item keys 1990000/1/2.
    //  B  state diff: snapshot climb/control/anim/equip objects while holding
    //     (twice, to drop per-frame noise) and after leaving the wall; log only
    //     fields that are stable on the wall and differ off it. That is where the
    //     current action-chart node / animation index and the stow state live.
    //  C  strings: printable names reachable from the control component that
    //     look like climb animation/node names, hold vs off.
    // ======================================================================
    bool IsKey(uint32_t v) { return v == kItemAxeAdze || v == kItemAxeHammer || v == kItemBoots; }
    std::string Short(const char* r) { return r ? std::string(r + 4, strcspn(r + 4, "@")) : std::string("?"); }
    bool RttiHas(uintptr_t obj, const char* needle) { const char* r = mem::RttiOfObject(obj); return r && strstr(r, needle); }

    size_t ReadUpTo(uintptr_t a, uint8_t* out, size_t want)
    {
        for (size_t n = want; n >= 0x40; n /= 2) if (mem::Read(a, out, n)) return n;
        return 0;
    }

    struct Hit { uint32_t key; std::string chain; uintptr_t block; size_t off; };

    // Deep bounded search below one root: follows every plausible heap pointer
    // (typed or not) to `maxDepth`, reading 0x200 bytes per block (root: 0x1000).
    void KeySearch(uintptr_t root, const std::string& rootName, int maxDepth, int budget, std::vector<Hit>& hits)
    {
        struct N { uintptr_t a; int d; std::string chain; };
        std::vector<N> q; q.push_back({ root, 0, rootName });
        std::vector<uintptr_t> seen; seen.push_back(root);
        static uint8_t buf[0x1000];
        for (size_t qi = 0; qi < q.size() && !g_stop; ++qi)
        {
            const N nd = q[qi];
            const size_t n = ReadUpTo(nd.a, buf, nd.d == 0 ? 0x1000 : 0x200);
            if (!n) continue;
            for (size_t o = 0; o + 4 <= n; o += 4)
            {
                uint32_t v; memcpy(&v, buf + o, 4);
                if (IsKey(v) && hits.size() < 64)
                {
                    char t[24]; snprintf(t, sizeof t, "+0x%zX", o);
                    hits.push_back({ v, nd.chain + t, nd.a, o });
                }
            }
            if (nd.d >= maxDepth) continue;
            for (size_t o = 0; o + 8 <= n && (int)q.size() < budget; o += 8)
            {
                uintptr_t p; memcpy(&p, buf + o, 8);
                if (!mem::PlausibleHeap(p) || mem::InImage(p)) continue;
                bool dup = false; for (auto s : seen) if (s == p) { dup = true; break; }
                if (dup) continue;
                seen.push_back(p);
                const char* r = mem::RttiOfObject(p);
                // Do not wander into unrelated managers/UI/physics through typed objects.
                if (r && (strstr(r, "UIGamePlay") || strstr(r, "Manager") || strstr(r, "hk") || strstr(r, "Scene") ||
                          strstr(r, "Actor@") || strstr(r, "Lock"))) continue;
                char t[24]; snprintf(t, sizeof t, "+0x%zX>", o);
                q.push_back({ p, nd.d + 1, nd.chain + t + (r ? Short(r) : std::string("[raw]")) });
            }
        }
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

    struct PresenceResult { bool adze = false, hammer = false, boots = false; std::vector<Hit> hits; };

    PresenceResult Presence(const Comps& c, int sess)
    {
        PresenceResult pr;
        if (c.inv)
        {
            LOGI("[presence %d] inventory component %s at %s (%p)", sess, Short(mem::RttiOfObject(c.inv)).c_str(), c.invPath.c_str(), (void*)c.inv);
            KeySearch(c.inv, "inv", 5, 4000, pr.hits);
        }
        else LOGI("[presence %d] no *InventoryActorComponent reachable from the climbing actor (checked its component list and a 2500-object walk).", sess);
        const size_t invHits = pr.hits.size();
        if (c.equip) KeySearch(c.equip, "equip", 4, 2500, pr.hits);
        for (size_t i = 0; i < pr.hits.size() && i < 16; ++i)
            LOGI("[presence %d] key %u at %s", sess, pr.hits[i].key, pr.hits[i].chain.c_str());
        for (size_t i = 0; i < invHits && i < 6; ++i)
        {
            // Context around each inventory hit: the item count / slot fields sit next to the key.
            uint32_t ctx[16] = {}; const uintptr_t at = pr.hits[i].block + pr.hits[i].off;
            if (mem::Read(at - 0x20, ctx, sizeof ctx))
                LOGI("[presence %d] ctx key %u @%s: [-0x20..+0x20) %08X %08X %08X %08X %08X %08X %08X %08X | %08X %08X %08X %08X %08X %08X %08X %08X",
                     sess, pr.hits[i].key, pr.hits[i].chain.c_str(), ctx[0], ctx[1], ctx[2], ctx[3], ctx[4], ctx[5], ctx[6], ctx[7],
                     ctx[8], ctx[9], ctx[10], ctx[11], ctx[12], ctx[13], ctx[14], ctx[15]);
        }
        for (size_t i = 0; i < invHits; ++i)
        {
            pr.adze |= pr.hits[i].key == kItemAxeAdze; pr.hammer |= pr.hits[i].key == kItemAxeHammer; pr.boots |= pr.hits[i].key == kItemBoots;
        }
        LOGI("[presence %d] inventory verdict: AxeAdze(1990000)=%s AxeHammer(1990001)=%s Crampons(1990002)=%s  (%zu inventory hit(s), %zu equip hit(s))",
             sess, pr.adze ? "PRESENT" : "not found", pr.hammer ? "PRESENT" : "not found", pr.boots ? "PRESENT" : "not found",
             invHits, pr.hits.size() - invHits);
        return pr;
    }

    // ---- B: snapshots --------------------------------------------------------
    struct Snap { std::string name; uintptr_t addr = 0; size_t n = 0; std::vector<uint8_t> d; };
    using SnapSet = std::vector<Snap>;

    void Add(SnapSet& s, const char* name, uintptr_t a, size_t want)
    {
        Snap sn; sn.name = name; sn.addr = a; sn.d.resize(want);
        if (a) sn.n = ReadUpTo(a, sn.d.data(), want);
        if (a && mem::RttiOfObject(a)) sn.name += std::string(" (") + Short(mem::RttiOfObject(a)) + ")";
        s.push_back(std::move(sn));
    }

    SnapSet TakeSnaps(uintptr_t self, const Comps& c, const PresenceResult* pr)
    {
        SnapSet s;
        uintptr_t anim = 0, mover = 0; mem::Get(c.ctrl + 0x110, anim); mem::Get(c.ctrl + 0x140, mover);
        Add(s, "climb", self, 0x400);
        Add(s, "ctrl=[climb+0x18]", c.ctrl, 0x800);
        Add(s, "anim=[ctrl+0x110]", anim, 0x400);
        Add(s, "move=[ctrl+0x140]", mover, 0x400);
        Add(s, "equip", c.equip, 0x1000);
        for (int i = 0; i < c.nStat; ++i) { char nm[24]; snprintf(nm, sizeof nm, "stat%d", i); Add(s, nm, c.stat[i], 0x800); }
        if (pr)
        {
            int k = 0;
            for (auto& h : pr->hits) if (k < 4) { char nm[96]; snprintf(nm, sizeof nm, "keyblock(%u @ %s)", h.key, h.chain.c_str()); Add(s, nm, h.block, 0x200); ++k; }
        }
        return s;
    }

    // Log fields that are identical in h1/h2 (stable while holding) and differ in off.
    void DiffSnaps(int sess, const SnapSet& h1, const SnapSet& h2, const SnapSet& off)
    {
        for (size_t i = 0; i < h1.size() && i < h2.size() && i < off.size(); ++i)
        {
            const Snap &a = h1[i], &b = h2[i], &o = off[i];
            if (!a.n) { LOGI("[diff %d] %s: unreadable", sess, a.name.c_str()); continue; }
            if (a.addr != o.addr || a.addr != b.addr) { LOGI("[diff %d] %s: object moved hold=%p/%p off=%p", sess, a.name.c_str(), (void*)a.addr, (void*)b.addr, (void*)o.addr); continue; }
            const size_t n = std::min(std::min(a.n, b.n), o.n);
            int shown = 0, noise = 0, stable = 0;
            for (size_t off4 = 0; off4 + 4 <= n; off4 += 4)
            {
                uint32_t va, vb, vo; memcpy(&va, &a.d[off4], 4); memcpy(&vb, &b.d[off4], 4); memcpy(&vo, &o.d[off4], 4);
                if (va != vb)
                {
                    ++noise;
                    // Stat components: per-frame drift while on the wall is exactly what stamina drain looks like.
                    if (a.name.find("stat") == 0 && noise <= 24)
                    {
                        float fa, fb, fo; memcpy(&fa, &va, 4); memcpy(&fb, &vb, 4); memcpy(&fo, &vo, 4);
                        LOGI("[drift %d] %s +0x%03zX  hold1=%08X (%d, %g)  hold2=%08X (%d, %g)  off=%08X (%d, %g)", sess, a.name.c_str(), off4,
                             va, (int)va, (double)fa, vb, (int)vb, (double)fb, vo, (int)vo, (double)fo);
                    }
                    continue;
                }
                if (va == vo) continue;
                ++stable;
                if (shown < 40)
                {
                    float f; memcpy(&f, &va, 4); float fo; memcpy(&fo, &vo, 4);
                    LOGI("[diff %d] %s +0x%03zX  hold=%08X (%d, %g)  off=%08X (%d, %g)", sess, a.name.c_str(), off4,
                         va, (int)va, (double)f, vo, (int)vo, (double)fo);
                    ++shown;
                }
            }
            LOGI("[diff %d] %s: %d stable hold-vs-off change(s), %d per-frame noisy dword(s) ignored", sess, a.name.c_str(), stable, noise);
        }
    }

    // ---- C: names --------------------------------------------------------------
    bool Interesting(const char* s)
    {
        static const char* kw[] = { "climb", "Climb", "CLIMB", ".paa", "hang", "Hang", "piolet", "axe", "Axe", "stow", "Weapon" };
        for (auto k : kw) if (strstr(s, k)) return true;
        return false;
    }
    void Names(uintptr_t root, std::vector<std::string>& out)
    {
        struct N { uintptr_t a; int d; std::string chain; };
        std::vector<N> q{ { root, 0, "ctrl" } }; std::vector<uintptr_t> seen{ root };
        static uint8_t buf[0x400];
        for (size_t qi = 0; qi < q.size() && q.size() < 2500 && out.size() < 200; ++qi)
        {
            const N nd = q[qi]; const size_t n = ReadUpTo(nd.a, buf, 0x400); if (!n) continue;
            // inline strings
            for (size_t i = 0; i < n; )
            {
                size_t j = i; while (j < n && buf[j] >= 0x20 && buf[j] < 0x7F) ++j;
                if (j - i >= 6) { std::string s((const char*)buf + i, j - i); if (Interesting(s.c_str())) { char t[24]; snprintf(t, sizeof t, "+0x%zX", i); out.push_back(nd.chain + t + " \"" + s + "\""); } }
                i = j + 1;
            }
            if (nd.d >= 3) continue;
            for (size_t o = 0; o + 8 <= n; o += 8)
            {
                uintptr_t p; memcpy(&p, buf + o, 8);
                if (!mem::PlausibleHeap(p) && !(p && mem::InImage(p))) continue;
                bool dup = false; for (auto s : seen) if (s == p) { dup = true; break; } if (dup) continue; seen.push_back(p);
                char t[24]; snprintf(t, sizeof t, "+0x%zX>", o);
                if (mem::InImage(p)) continue; // vtables / static strings: not per-character state
                const char* r = mem::RttiOfObject(p);
                if (r && (strstr(r, "UIGamePlay") || strstr(r, "Manager") || strstr(r, "hk") || strstr(r, "Actor@"))) continue;
                q.push_back({ p, nd.d + 1, nd.chain + t + (r ? Short(r) : std::string("[raw]")) });
            }
        }
    }

    void LogNames(int sess, const std::vector<std::string>& hold, const std::vector<std::string>& off)
    {
        int k = 0;
        for (auto& h : hold)
        {
            bool inOff = false; for (auto& o : off) if (o == h) { inOff = true; break; }
            if (inOff && sess > 1) continue;
            if (k++ >= 40) break;
            LOGI("[names %d] %s%s", sess, inOff ? "" : "ONLY-ON-WALL ", h.c_str());
        }
        LOGI("[names %d] %zu name(s) on the wall, %zu off it", sess, hold.size(), off.size());
    }

    // ---- worker --------------------------------------------------------------
    DWORD WINAPI Worker(LPVOID)
    {
        constexpr unsigned kU2 = 1; // index of update2 in kSlotLabel
        DWORD lastSummary = GetTickCount(), lastU2Change = 0, sessStart = 0;
        long prev[kSlots] = {}, lastU2 = 0;
        bool climbing = false; int sess = 0, grabs = 0, probed = 0;
        uintptr_t self = 0;
        Comps comps; PresenceResult pres; SnapSet h1, h2; std::vector<std::string> namesHold;
        enum { Idle, WantH1, WantH2, WantOff } phase = Idle;
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
                pres = Presence(comps, sess);
                h1 = TakeSnaps(self, comps, &pres);
                namesHold.clear(); if (comps.ctrl) Names(comps.ctrl, namesHold);
                phase = WantH2;
            }
            else if (climbing && phase == WantH2 && now - sessStart >= 1400)
            {
                h2 = TakeSnaps(self, comps, &pres);
                phase = WantOff;
            }
            if (climbing && now - lastU2Change > 1500)
            {
                climbing = false;
                LOGI("[climb] session %d ended: %lu ms on the wall, %d grab(s)", sess, (unsigned long)(lastU2Change - sessStart), grabs);
                if (phase == WantOff)
                {
                    SnapSet off = TakeSnaps(self, comps, &pres);
                    DiffSnaps(sess, h1, h2, off);
                    std::vector<std::string> namesOff; if (comps.ctrl) Names(comps.ctrl, namesOff);
                    LogNames(sess, namesHold, namesOff);
                }
                else if (phase != Idle) LOGI("[climb] session %d too short for the hold-vs-off diff (need >1.4 s on the wall).", sess);
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
