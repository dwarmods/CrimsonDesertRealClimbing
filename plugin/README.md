# Climbmod ASI 0.1.1-diag

Targets (exactly these two; anything else fails closed):
- CrimsonDesert.exe 1.0.0.3663 (Nov 9 2026 update), sha256
  8be67178d9b9a149c361a731051665e0ba4d441e250ae1dfe645bd6e524345c8,
  PE TimeDateStamp 6AF5D198, SizeOfImage 144E9000, CheckSum 13FF2BD0.
- CrimsonDesert.exe 1.0.0.3576 (2.04.08), sha256
  c94ad6c3ea54970a6d2da7a2719753210a7728a91698ed3405dd9125470ed98f,
  PE TimeDateStamp 6AED2E85, SizeOfImage 13CC4000, CheckSum 137C1424.

0.1.1 changes: per-build address table (src/build_info.h) selected by PE
identity; 3663 addresses re-proven by static analysis (research/port3663*.py,
research/static_findings_3663.json, research/gate_replay.json); LogLevel
defaults to 2. Behaviour is otherwise identical to 0.1.0-diag.

3663 re-location evidence: each of the six RTTI names resolves to exactly one
vtable (PointClimb 0x59A2B48, LadderClimb 0x59A2BA0, Client/Common/Server
EquipSlot 0x59C90F8/0x5D43420/0x5F6C518, ClientSkill 0x59D02F0); slot counts
unchanged (10/10/67/67/67/54); PointClimb overrides the same ICharacter-
MovementMethod slots; slots 4 (0x78E5F0) and 6 (0x78E9A0) are instruction-
identical to 3576, slots 7 begin (0x78D520) and 8 end (0x78D930) differ only
by member offsets (object grew by 0x58) and still read [this+0x18]; slot 1
update (0x78DEE0) is at the same index but was recompiled (similarity 0.67);
it is only counted and passed through. The equip-condition worker moved to
game+0x189C900 (same reads actor->[+0x68]->[+0x38], calls game+0x22FADE0);
the plugin does not call it. All hooks proven; none disabled.

**This is the diagnostic build. It does not swap animations and does not keep
the axes drawn.** AnimSwap / KeepAxesDrawn are accepted in the ini, logged, and
ignored. Vanilla climbing is untouched with or without the axes.

## What it does
1. Only inside CrimsonDesert.exe (crashpad_handler.exe etc. -> silent no-op).
2. Fail-closed identity gate: PE TimeDateStamp/SizeOfImage/CheckSum must match,
   and six vtables must carry the expected RTTI names
   (PointClimb/LadderClimb MovementMethod, Client/Common/Server
   EquipSlotActorComponent, ClientSkillActorComponent). Any mismatch: one log
   line, nothing hooked.
3. Observes `pa::PointClimbMovementMethod` by swapping 5 of its own vtable
   slots (1 update, 4 update2, 6 post_update, 7 begin, 8 end) to
   register-preserving thunks (src/thunks.S) that count the call and jump to
   the original. Each slot is swapped only if it still holds the vanilla
   target (another mod owning it -> left alone). No game code bytes are patched.
4. On climb begin (slot 7) the worker thread (never the game thread) does a
   bounded, read-only breadth-first walk over RTTI-typed objects reachable from
   the climb object, logs every EquipSlot component it reaches and every place
   item keys 1990000 / 1990001 / 1990002 appear, and prints a heuristic
   "seen under an EquipSlot component" verdict.
5. Log: ClimbmodASI.log next to the .asi (LogLevel 0/1/2, default 2). With DMM
   the .asi is deployed to the game's bin64, so the log is bin64\ClimbmodASI.log.

## Proven by static analysis (exe on disk)
- vtable RVAs + RTTI names in src/build_info.h; full slot tables in
  research/static_findings.json.
- Slot roles from comparing all 20 ICharacterMovementMethod subclasses: base
  slot 7 is `mov al,1; ret` (begin -> bool), PointClimb slot 7 takes a bool in
  dl and initialises climb state, slot 8 tears it down, slots 1/3 are the
  update pair (3 is `jmp` to 1).
- Every PointClimb slot reads its owner at [this+0x18].
- (3576) pa::AICondition_CheckEquipSlotNameItem evaluates through game+0x1837150,
  which reads actor->[+0x68]->[+0x38] (equip-slot component) and queries it via
  game+0x228A440 (inside ServerEquipSlotActorComponent code).

## Needs an in-game test
- That PointClimbMovementMethod is the climb used for PointClimbHold /
  basic_lower_climb holds (vs. free climb on another path), i.e. that
  begin/end lines appear when you grab a hold.
- Where the equipped axe keys live (the scan prints the paths).

## Remaining blocker for the real clip swap
The animation-resource selector (what turns basic_lower_climb anim59/node18
and anim60/node52 into cd_phm_basic_00_00_freeclimb_std_around_idle_1_00.paa)
and the part-draw op for ModelAttachmentSocket event 522804 are not identified
in the exe yet. Without them there is nothing proven to hook, so the swap is
not shipped. Next step: run this build, send ClimbmodASI.log (LogLevel=2), then
trace from the climb owner ([this+0x18]+0xC8, the component slot 8 calls into)
to the motion player.

## Build
`./build.sh` (Linux, `pip install ziglang`) -> dist/ClimbmodASI.asi.

## 0.1.2-diag (Nov 15 2026)

What the 0.1.1 run (exe 3663, 01:03-01:05 log) proved:
- Hooks fine. `begin` (slot 7) fires on EVERY grab/reposition (54x in ~2 min), `end` (slot 8) only once;
  `update2`/`post_update` tick ~25-37/s only while on the wall; slot 1 `update` never fires for PointClimb.
  So "on the wall" = update2 ticking; a climb session ends when update2 stops for >1.5 s.
- climb object is persistent (same `this`), owner [climb+0x18] = ClientCharacterControlActorComponent,
  [climb+0x20] = the character actor (ClientChildOnlyInGameActor), its component array [actor+0x68],
  entry [+0x38] = ClientEquipSlotActorComponent (RTTI-verified, matches static actor->+0x68->+0x38).
- Item keys 1990000-2 were NOT found as dwords in the equip component, one raw hop, or 600 objects of
  graph walk. Equip-slot detection is not established (and is now moot: design is inventory presence).

Design (Stas, Nov 15): no equipping. Presence in INVENTORY drives everything:
- one axe (1990000 or 1990001) -> single-hand plant/hold clip + one hand prop; both -> dual clip + both props;
  crampons (1990002) -> boot visual/foot benefit independently. Props shown only while climbing, hidden after.
  Player equipment is never modified.
- climb-skill stamina costs scaled per item present (INI multipliers, stacked; one axe < two axes, + crampons),
  Stamina Master style scaling of the resource use restricted to climb skills. Replaces equip stat 1000037
  (which should later be removed from the gear pack items so the bonus is not double-counted when equipped).
- rope rest (authored rope_rest clips, animation-only, no items) slowly regenerates stamina, INI rate relative
  to ground regen.
Resources already shipped unrouted in gear pack 0.3.0:
`character/motion/1_pc/1_phm/climbmod/climbmod_piolet_{dual,left,right}_plant_hold_00.paa`.

0.1.2-diag probes (read-only, worker thread, LogLevel 1 is targeted; no graph dumps):
- [presence] component list of the climbing actor by RTTI, *InventoryActorComponent found by name, deep
  key search (4000 blocks) under inventory and equip components, verdict per item + 64-byte context
  around each key (count/slot fields).
- [diff] snapshots of climb, ctrl, [ctrl+0x110] anim, [ctrl+0x140] mover, equip, stat components and key
  blocks at +0.6 s and +1.4 s on the wall and again off the wall; logs only fields stable on the wall and
  different off it (animation node/index, stow state). [drift] logs stat-component fields changing while
  on the wall (stamina drain).
- [names] climb/axe/weapon-like strings reachable from ctrl, flagged ONLY-ON-WALL.
Still unproven, so not implemented: animation selector hook, hand-prop attach, stamina scaling, rope regen.

## 0.1.3-diag (Nov 15 2026)

What the 0.1.2-diag run (01:34-01:38, 5 sessions) proved:
- The actor at [climb+0x20] has 57 components; [actor+0x68][+0xB8] = ClientInventoryActorComponent,
  [+0x20] = ClientStatusActorComponent, [+0x38] = equip, [+0x40] = character control.
- 0 hits for 1990000/1/2 under the inventory in all sessions, but 0.1.2 had no control: it never
  checked that ANY item key was reachable, so "not carried" and "search can't reach the item list"
  were indistinguishable.
- Item keys are plain u32 in memory: off the wall [equip+0x40] = 1000771 (the in-hand item key),
  while on the wall it is -1 (hand emptied = the vanilla stow on climb).
- Wall-vs-off leads: [[ctrl+0x140]+0x10] = 4 on the wall vs 3 off (movement mode);
  ClientStatusActorComponent +0x477 byte = 1 on the wall, 0 off (3 of 3 sessions);
  [ctrl+0x110] is not an RTTI object (no diffs there); the names probe found nothing (names are hashed).
  Stamina is not inside the first 0x800 bytes of ClientStatusActorComponent.

0.1.3-diag: [inv] walks the inventory 6 levels / 6000 blocks, lists every item-key-like value
(1000000..2099999) with count and path, uses the in-hand key as a control, and logs keys added/removed
since the previous climb; [equip] logs key-like values in the equip component on and off the wall;
[state] diffs ctrl/move/stat subtrees (2 levels) on the wall x2 vs off x2: small-int fields that
flip (anim node / mode / stow), and values that fall on the wall and rise off it (stamina candidates).
