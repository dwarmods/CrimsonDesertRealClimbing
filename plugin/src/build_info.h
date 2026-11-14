#pragma once
#include <cstdint>
// Every constant here was read off the exe on disk by research/pe.py,
// research/static_findings.py (3576) and research/port3663*.py (3663).
// Nothing is trusted at runtime without being re-checked against the live
// image: PE identity first (selects the table below), then the RTTI name behind
// every vtable, then every slot value we touch. Any other build: fail closed.
namespace cm::build
{
    inline constexpr const char* kVersion = "0.1.3-diag";

    struct Vt { uint32_t rva; const char* rtti; };
    // PointClimbMovementMethod slots (ICharacterMovementMethod layout):
    // 1 per-frame update, 4 second update, 6 post-update, 7 begin(bool), 8 end.
    struct Slot { unsigned index; uint32_t rva; const char* label; };
    inline constexpr unsigned kSlotCount = 5;
    inline constexpr unsigned kSlotIndex[kSlotCount] = { 1, 4, 6, 7, 8 };
    inline constexpr const char* kSlotLabel[kSlotCount] = { "update", "update2", "post_update", "begin", "end" };

    struct BuildDef
    {
        const char* label;
        uint32_t timeDateStamp, sizeOfImage, checkSum;
        Vt pointClimb, ladderClimb, clientEquipSlot, commonEquipSlot, serverEquipSlot, clientSkill;
        uint32_t slotRva[kSlotCount];
        uint32_t ownerOffset; // [this+0x18] read by begin/end on both builds
    };

    // Crimson Desert 2.04.08, CrimsonDesert.exe 1.0.0.3576,
    // sha256 c94ad6c3ea54970a6d2da7a2719753210a7728a91698ed3405dd9125470ed98f
    inline constexpr BuildDef k3576 {
        "CrimsonDesert.exe 1.0.0.3576 (2.04.08)",
        0x6AED2E85u, 0x13CC4000u, 0x137C1424u,
        { 0x05926780u, ".?AVPointClimbMovementMethod@pa@@" },
        { 0x059267D8u, ".?AVLadderClimbMovementMethod@pa@@" },
        { 0x0594D478u, ".?AVClientEquipSlotActorComponent@pa@@" },
        { 0x05CC7960u, ".?AVCommonEquipSlotActorComponent@pa@@" },
        { 0x05EF28B0u, ".?AVServerEquipSlotActorComponent@pa@@" },
        { 0x05954400u, ".?AVClientSkillActorComponent@pa@@" },
        { 0x00761A30u, 0x00761FA0u, 0x00762350u, 0x00761070u, 0x00761480u },
        0x18,
    };

    // Nov 9 2026 update, CrimsonDesert.exe 1.0.0.3663,
    // sha256 8be67178d9b9a149c361a731051665e0ba4d441e250ae1dfe645bd6e524345c8
    // Each RTTI name has exactly one vtable; all 7 vtables keep their slot
    // counts (10/10/10/67/67/67/54) and PointClimb keeps the same override
    // pattern vs ICharacterMovementMethod. Slots 4/6 are instruction-identical
    // to 3576, slots 7/8 differ only in member offsets (object grew 0x58) and
    // still read [this+0x18]; slot 1 (update) was recompiled (sim 0.67).
    inline constexpr BuildDef k3663 {
        "CrimsonDesert.exe 1.0.0.3663 (Nov 9 2026 update)",
        0x6AF5D198u, 0x144E9000u, 0x13FF2BD0u,
        { 0x059A2B48u, ".?AVPointClimbMovementMethod@pa@@" },
        { 0x059A2BA0u, ".?AVLadderClimbMovementMethod@pa@@" },
        { 0x059C90F8u, ".?AVClientEquipSlotActorComponent@pa@@" },
        { 0x05D43420u, ".?AVCommonEquipSlotActorComponent@pa@@" },
        { 0x05F6C518u, ".?AVServerEquipSlotActorComponent@pa@@" },
        { 0x059D02F0u, ".?AVClientSkillActorComponent@pa@@" },
        { 0x0078DEE0u, 0x0078E5F0u, 0x0078E9A0u, 0x0078D520u, 0x0078D930u },
        0x18,
    };

    inline constexpr const BuildDef* kBuilds[] = { &k3663, &k3576 };

    // climbmod item keys.
    inline constexpr uint32_t kItemAxeAdze   = 1990000u;
    inline constexpr uint32_t kItemAxeHammer = 1990001u;
    inline constexpr uint32_t kItemBoots     = 1990002u;
}
