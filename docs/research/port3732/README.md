# Port to exe 1.0.0.3732 (Nov 19 2026 update)

Identity: PE TimeDateStamp `0x6AFDC0CF`, SizeOfImage `0x13C7E000`, CheckSum `0x13787B92`.

| RTTI type | 3663 vtable RVA | 3732 vtable RVA |
|---|---|---|
| PointClimbMovementMethod | 0x059A2B48 | 0x059A7C90 |
| LadderClimbMovementMethod | 0x059A2BA0 | 0x059A7C38 |
| ClientEquipSlotActorComponent | 0x059C90F8 | 0x059CE1D8 |
| CommonEquipSlotActorComponent | 0x05D43420 | 0x05D48810 |
| ServerEquipSlotActorComponent | 0x05F6C518 | 0x05F71898 |
| ClientSkillActorComponent | 0x059D02F0 | 0x059D53E8 |

PointClimb hooked slots 1/4/6/7/8: 3732 = 0x0078E400, 0x0078ED10, 0x0078F0C0, 0x0078DA40, 0x0078DE50.
Slots 4/6/7/8 have normalized-disassembly similarity 1.0 to 3663. Slot 1 was recompiled
(similarity 0.38) but keeps the same slot and role. Its thunk only logs, then calls the original.

The equip-condition worker moved from 0x189c900 to 0x189e790 (signature similarity 1.0).
It still reads `[[actor+0x68]+0x38]`, so the component-list layout is unchanged. See
`layout3732.json` for the component vtable similarity.

Offline gate replay: `gate_replay.json` (`tools/re/verify_gate.py`). Both k3663 and k3732 pass.

## Gear pack on 3732

ItemInfo's row format changed in 3732. All 7068 vanilla rows differ from the 3663
rows, and CDMW 0.12.0-alpha.5's row parser fails on every one. DMM's runtime table merge
also reports that it cannot read the game's iteminfo, so it skips the pack's iteminfo.
Items 1990000/1/2 are therefore not registered on 3732 until a 3732-aware CDMW/DMM ships.
The other tables the pack edits (dropsetinfo, multichangeinfo, partprefabdyeslotinfo,
stringinfo) keep their row format, and the pack's vanilla rows match 3732 byte-for-byte.
