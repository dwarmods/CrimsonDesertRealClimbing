# Design

See the design rules in the top-level README. This file tracks how each rule maps
onto the runtime plugin and what is proven on exe 1.0.0.3663.

| Feature | Mechanism (plan) | Status |
|---|---|---|
| Detect climbing | PointClimbMovementMethod vtable slots: begin (7) per grab, update2 (4) per frame on the wall | **proven in game** (0.1.1-diag log) |
| Inventory presence 1990000/1/2 | find `*InventoryActorComponent` on the climbing actor by RTTI, read item keys + counts | probe in 0.1.2-diag |
| One axe / two axes / crampons scaling | per-item flags from presence → clip choice + props + stamina multipliers | design |
| Authored plant/hold clips | `character/motion/1_pc/1_phm/climbmod/climbmod_piolet_{right,left,dual}_plant_hold_00.paa` shipped unrouted in the gear pack; needs the animation selector hook | blocked: selector not located |
| Axes in hand as props | attach piolet meshes to RHand/LHand sockets only while on the wall; never touch real equipment | blocked: attach API not located |
| Climb-only stamina multipliers | Stamina Master-style scaling of resource use, filtered to climb skills (PointClimbHold SkillInfo 10058 and climb moves) | blocked: needs the stamina use site on 3663 |
| Rope-rest regen | while in rope_rest_loop, regen at INI fraction of ground regen | design (rope clips are animation-only) |

## INI plan (1.0)
```
[climbmod]
AnimSwap=1            ; play authored plant/hold clips when an axe is carried
HandProps=1           ; show carried axes in hand while climbing
StaminaOneAxe=0.85    ; climb stamina cost multiplier with one axe
StaminaTwoAxes=0.70   ; ... with both axes
StaminaCrampons=0.85  ; multiplies on top of the axe multiplier
RopeRestRegen=0.35    ; fraction of on-ground stamina regen while resting on the rope
```

## Note on the gear pack
The gear pack items currently carry an equip stat (StatusInfo 1000037, 15%/15%/20%).
With inventory-presence scaling that stat double-counts when the items are also
equipped; remove it from the items in the next gear-pack rebuild.
