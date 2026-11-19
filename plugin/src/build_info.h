#pragma once
#include <cstdint>
// Every constant here was read off the exe on disk by research/pe.py,
// research/static_findings.py  and research/port3663*.py (3663).
// Nothing is trusted at runtime without being re-checked against the live
// image: PE identity first (selects the table below), then the RTTI name behind
// every vtable, then every slot value we touch. Any other build: fail closed.
namespace cm::build
{
    inline constexpr const char* kVersion = "0.1.6-diag";

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

    // Nov 19 2026 update, CrimsonDesert.exe 1.0.0.3732,
    // sha256 dfcf5c507d09aa0e09cf9766db866fd1e7cf513b9dd7fdc72a8f121ba2d9a9d5
    // research/port3732.py: each RTTI name still has exactly one vtable with the
    // same slot count; PointClimb slots 4/6/7/8 are instruction-identical to 3663
    // (similarity 1.0, so [this+0x18]/[this+0x20] reads are unchanged); slot 1
    // (update, never called for PointClimb in practice) was recompiled (sim 0.38)
    // but is still an override in the same position. The equip worker still reads
    // actor->[+0x68]->[+0x38] (research/port3732_layout.py).
    inline constexpr BuildDef k3732 {
        "CrimsonDesert.exe 1.0.0.3732 (Nov 19 2026 update)",
        0x6AFDC0CFu, 0x13C7E000u, 0x13787B92u,
        { 0x059A7C90u, ".?AVPointClimbMovementMethod@pa@@" },
        { 0x059A7C38u, ".?AVLadderClimbMovementMethod@pa@@" },
        { 0x059CE1D8u, ".?AVClientEquipSlotActorComponent@pa@@" },
        { 0x05D48810u, ".?AVCommonEquipSlotActorComponent@pa@@" },
        { 0x05F71898u, ".?AVServerEquipSlotActorComponent@pa@@" },
        { 0x059D53E8u, ".?AVClientSkillActorComponent@pa@@" },
        { 0x0078E400u, 0x0078ED10u, 0x0078F0C0u, 0x0078DA40u, 0x0078DE50u },
        0x18,
    };

    // Newest first. 1.0.0.3576 was dropped in 0.1.6 (superseded twice).
    inline constexpr const BuildDef* kBuilds[] = { &k3732, &k3663 };

    // climbmod item keys.
    inline constexpr uint32_t kItemAxeAdze   = 1990000u;
    inline constexpr uint32_t kItemAxeHammer = 1990001u;
    inline constexpr uint32_t kItemBoots     = 1990002u;
}
