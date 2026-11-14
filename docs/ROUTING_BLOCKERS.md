# Why the authored clips do not play from data alone (gear pack 0.3.0)

Summary of the data-routing investigation (basic_lower / basic_lower_climb action charts):

- The three authored loops ship as UNROUTED resources under
  `character/motion/1_pc/1_phm/climbmod/`; nothing references them.
- `basic_lower.paac` animations 401-404 (nodes 211/82) carry the generic
  ModelAttachmentSocket event 522804 (mode0=In, all_parts=1, t=0) that stows all
  weapon parts when climbing starts; Kliff MainWeapon_Axe_R/L are Out at the hand,
  In at the pelvis. Changing that shared event would affect every weapon.
- Native `ConditionInfo` has CheckEquipItem, but PAAC condition bytecode uses a
  different serialization; no item predicate is proven inside climb charts, so
  no gear-conditioned branch can be authored safely in data.
- Stationary hold: `basic_lower_climb` animations 59 (node 18) and 60 (node 52)
  → freeclimb_std_around_idle. Move-L/R (87/69, 88/60) are step blends, not holds.

Hence the runtime plugin route: detect hold + carried gear, then swap the clip and
show props. The selector/attach/stamina sites are the open work (see DESIGN.md).
