# Rope / anchor visuals: runtime trigger design (DESIGN ONLY, nothing here is implemented)

Status: the rope slice (5 clips + 4 props + baked rope chain) is staged as
UNROUTED resources in the rope slice work folder. No game file and
no climb chart references them. ClimbmodASI 0.1.0-diag does not play them.
Visual only: no ItemInfo rows, no inventory, no placeable items (per Stas).

## Clips (Kliff phm_01, 448 bones, 30 Hz, model space Y-up, faces -Z, wall z=-0.50)
| clip | frames | starts | ends |
|---|---|---|---|
| climbmod_rope_place_anchor_00 | 78 (2.57 s) | dual hold (plant/hold f0) | L hammer near piton |
| climbmod_rope_clip_00 | 61 (2.0 s) | place_anchor last frame | dual hold |
| climbmod_rope_rest_enter_00 | 49 (1.6 s) | dual hold | rest_loop f0 |
| climbmod_rope_rest_loop_00 | 91 (3.0 s, loops exactly) | rest | rest |
| climbmod_rope_rest_exit_00 | 49 (1.6 s) | rest_loop f0 | dual hold |
All hold endpoints equal frame 0 of climbmod_piolet_dual_plant_hold_00, so the
two slices chain without a pop. Each clip has <clip>_samples.json with per-frame
world positions of tie-in, rack, tool points, piton/carabiner state and the rope
control points + slack, and <clip>_rope_props.glb with the baked prop motion.

## Proposed state machine (per player, plugin side)
    CLIMB_HOLD --(N climb moves since last anchor, or key A)--> PLACE_ANCHOR
    PLACE_ANCHOR (78f) --> CLIP_ROPE (61f) --> CLIMB_HOLD   [anchor_count++]
    CLIMB_HOLD --(key R held, anchor exists within ~1.6 m above)--> REST_ENTER (49f)
    REST_ENTER --> REST_LOOP (loop while key held) --(release / move input)--> REST_EXIT (49f) --> CLIMB_HOLD
    any state --(fall, ledge mantle, leave climb, hit, death, unequip axes)--> abort: despawn hand-held props, keep placed anchors for T s
Triggers:
- auto: every N (=6, ini) completed PointClimb move cycles, only while both
  climbmod axes are equipped and the player is in a stationary hold.
- manual: ini keys (default F6 anchor, hold F7 rest).
Gating is the same as the clip swap: 1990000/1990001 equipped (proved by the
diag scan), PointClimbMovementMethod active, stationary hold.

## Spawning the visuals
1. Anchor spot: at PLACE_ANCHOR f45 (hand-set), take the L carry point from
   the live skeleton (L hand bone x tool attachment x carry_local) and project
   it onto the wall along the controller's wall normal. Store a world
   transform: piton +Y (blade) = -wall_normal, ring down = gravity.
2. Piton depth: protrusion 0.085 -> 0.070 -> 0.055 -> 0.040 -> 0.028 m at f47/55/63/71
   (the three hammer contacts are flagged hammer_contact in the samples JSON;
   good frames for an iron-on-iron sound and a few sparks).
3. Carabiner: rack (pelvis frame, rack_pelvis_local) -> L carry point f13..26 ->
   clipped to the piton eye from f27, hanging toward the tie-in.
4. Rope: one chain object owned by the player. Control points per frame =
   [tie-in (pelvis frame, tie_in_pelvis_local), (hand bight f40..48), carabiner low point(s),
   belay/trail end]. Rebuild the 64 bone poses each frame from those points
   with the slack values in the samples (parabolic sag, kept off the wall).
   This is a cheap kinematic rebuild of the baked rule, not physics. Several
   placed anchors simply add carabiner points to the list.
5. Waist tie (bowline on a coil) is attached to Bip01 Pelvis for the whole climb
   (or only while a rope is out, ini toggle).

## Engine pieces this needs (all UNPROVEN today)
- Playing an arbitrary PAA on the player for N frames, then handing control
  back (same blocker as the plant/hold swap: animation selector not found).
- Spawning/attaching a mesh or skinned prop at runtime (a game-side
  "attach model to socket / world" call, or reusing an existing gimmick or
  rope-swing prop pipeline: RopeSwingMovementMethod and the rope gimmicks
  are the first candidates to read).
- Wall normal / hold point from the climb controller (PointClimbMovementMethod
  owner at [this+0x18]; the diag log will show what it carries).
- Converting the props to game PAC/PAM + DDS (meshes and BC7/BC5 textures are
  staged; no PAC build for non-item props has been done).
Until those are proven by the diag build plus static analysis, no runtime hook
for this is written.
