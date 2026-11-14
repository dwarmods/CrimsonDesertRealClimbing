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
        g_set.logLevel       = IniInt(L"LogLevel", 2);
        g_set.hookPointClimb = IniInt(L"HookPointClimb", 1) != 0;
        g_set.equipScan      = IniInt(L"EquipScan", 1) != 0;
        g_set.maxScans       = IniInt(L"MaxScans", 8);
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
    // ---- worker-side scan ------------------------------------------------------
    struct Node { uintptr_t addr; int depth; std::string path; const char* rtti; };

    struct KeyHit { uint32_t key; std::string where; bool underEquip; };

    void ScanBuffer(const uint8_t* b, size_t n, const std::string& path, bool underEquip, std::vector<KeyHit>& hits)
    {
        for (size_t o = 0; o + 4 <= n; o += 4)
        {
            uint32_t v; memcpy(&v, b + o, 4);
            if (v == kItemAxeAdze || v == kItemAxeHammer || v == kItemBoots)
            {
                char t[32]; snprintf(t, sizeof t, "+0x%zX", o);
                hits.push_back({ v, path + t, underEquip });
            }
        }
    }

    bool IsEquipVt(uintptr_t vt)
    {
        const uintptr_t b = mem::Game().base;
        return vt == b + g_b->clientEquipSlot.rva || vt == b + g_b->commonEquipSlot.rva || vt == b + g_b->serverEquipSlot.rva;
    }

    size_t ReadUpTo(uintptr_t a, uint8_t* out, size_t want)
    {
        for (size_t n = want; n >= 0x40; n /= 2) if (mem::Read(a, out, n)) return n;
        return 0;
    }

    // Breadth-first over RTTI-typed objects reachable from the climb method,
    // read-only and bounded. Reports every RTTI class seen (verbose), every
    // EquipSlot component found, and every place a climbmod item key appears.
    void ScanFrom(uintptr_t self, int scanNo)
    {
        std::vector<Node> q; q.reserve(g_set.nodeBudget);
        std::vector<uintptr_t> seen; seen.reserve(g_set.nodeBudget * 2);
        std::vector<KeyHit> hits;
        std::vector<std::string> equipPaths;
        auto visited = [&](uintptr_t a) { for (auto s : seen) if (s == a) return true; seen.push_back(a); return false; };

        q.push_back({ self, 0, "climb", mem::RttiOfObject(self) });
        visited(self);
        static uint8_t buf[0x1000], sub[0x400];
        for (size_t qi = 0; qi < q.size() && (int)q.size() < g_set.nodeBudget && !g_stop; ++qi)
        {
            const Node nd = q[qi];
            uintptr_t vt = 0; mem::Get(nd.addr, vt);
            const bool equip = IsEquipVt(vt);
            const bool underEquip = equip || nd.path.find("EquipSlot") != std::string::npos;
            const size_t want = equip ? 0x1000 : 0x400;
            const size_t n = ReadUpTo(nd.addr, buf, want);
            if (!n) continue;
            if (equip) { equipPaths.push_back(nd.path); LOGI("[scan %d] %s component at %s (%p)", scanNo, nd.rtti + 4, nd.path.c_str(), (void*)nd.addr); }
            ScanBuffer(buf, n, nd.path, underEquip, hits);
            for (size_t o = 8; o + 8 <= n; o += 8)
            {
                uintptr_t p; memcpy(&p, buf + o, 8);
                if (!mem::PlausibleHeap(p) || mem::InImage(p) || visited(p)) continue;
                char off[24]; snprintf(off, sizeof off, "+0x%zX", o);
                if (const char* r = mem::RttiOfObject(p))
                {
                    if (nd.depth < 4)
                    {
                        std::string path = nd.path + off + ">" + std::string(r + 4, strcspn(r + 4, "@"));
                        q.push_back({ p, nd.depth + 1, path, r });
                        LOGV("[scan %d] d%d %s", scanNo, nd.depth + 1, path.c_str());
                    }
                }
                else if (underEquip || nd.depth < 2)
                {
                    // Untyped block (an array or an item record): look for keys in it,
                    // and for typed objects one hop further.
                    const size_t m = ReadUpTo(p, sub, sizeof sub);
                    if (!m) continue;
                    std::string path = nd.path + off + ">[raw]";
                    ScanBuffer(sub, m, path, underEquip, hits);
                    for (size_t k = 0; k + 8 <= m && nd.depth < 3; k += 8)
                    {
                        uintptr_t c; memcpy(&c, sub + k, 8);
                        if (!mem::PlausibleHeap(c) || visited(c)) continue;
                        if (const char* r = mem::RttiOfObject(c))
                        {
                            char o2[24]; snprintf(o2, sizeof o2, "+0x%zX>", k);
                            q.push_back({ c, nd.depth + 1, path + o2 + std::string(r + 4, strcspn(r + 4, "@")), r });
                        }
                    }
                }
            }
        }
        bool adze = false, hammer = false, boots = false;
        for (auto& h : hits)
        {
            LOGI("[scan %d] item key %u at %s%s", scanNo, h.key, h.where.c_str(), h.underEquip ? "  (under an EquipSlot component)" : "");
            if (h.underEquip) { adze |= h.key == kItemAxeAdze; hammer |= h.key == kItemAxeHammer; boots |= h.key == kItemBoots; }
        }
        LOGI("[scan %d] visited %zu typed objects, %zu EquipSlot component(s), %zu key hit(s). Heuristic equip "
             "verdict: AxeAdze(1990000)=%s AxeHammer(1990001)=%s Boots(1990002)=%s",
             scanNo, q.size(), equipPaths.size(), hits.size(), adze ? "seen" : "not seen",
             hammer ? "seen" : "not seen", boots ? "seen" : "not seen");
        if (equipPaths.empty())
            LOGI("[scan %d] no EquipSlot component was reachable within the budget; raise ScanNodeBudget or "
                 "send this log back.", scanNo);
    }

    DWORD WINAPI Worker(LPVOID)
    {
        int scans = 0; DWORD beganAt = 0; uintptr_t lastSelf = 0;
        DWORD lastSummary = GetTickCount();
        long prev[kSlots] = {};
        while (!g_stop)
        {
            WaitForSingleObject(g_event, 1000);
            while (g_tail.load(std::memory_order_acquire) != g_head.load(std::memory_order_acquire))
            {
                unsigned t = g_tail.load(std::memory_order_relaxed);
                Ev e = g_ring[t % kRing];
                g_tail.store(t + 1, std::memory_order_release);
                if (e.kind == 7)
                {
                    beganAt = e.tick; lastSelf = e.self;
                    const char* r = mem::RttiOfObject(e.self);
                    uintptr_t owner = 0; mem::Get(e.self + g_b->ownerOffset, owner);
                    LOGI("[climb] PointClimb begin  this=%p (%s) owner=[this+0x18]=%p (%s)", (void*)e.self,
                         r ? r : "?", (void*)owner, mem::RttiOfObject(owner) ? mem::RttiOfObject(owner) : "untyped");
                    if (g_set.equipScan && scans < g_set.maxScans) ScanFrom(e.self, ++scans);
                }
                else
                    LOGI("[climb] PointClimb end    this=%p after %lu ms%s", (void*)e.self,
                         (unsigned long)(e.tick - beganAt), e.self == lastSelf ? "" : " (different object than begin)");
            }
            if (GetTickCount() - lastSummary >= 10000)
            {
                lastSummary = GetTickCount();
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
         "EquipScan=%d MaxScans=%d ScanNodeBudget=%d AnimSwap=%d KeepAxesDrawn=%d", kVersion, g_set.logLevel, g_set.enabled, g_set.hookPointClimb,
         g_set.equipScan, g_set.maxScans, g_set.nodeBudget, g_set.animSwap, g_set.keepAxesDrawn);
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
             "and part-draw hooks are not proven yet, so neither is applied. Vanilla animation is unchanged.");

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
