# Crimson Desert — Real Climbing

A mod project that makes climbing in Crimson Desert look and feel like real
mountaineering: ice axes (piolets) and crampons, plant-and-hold climbing
animations, and rope/anchor rests.

## Design rules

- **Inventory presence, not equipment.** Carrying the climbing gear is enough.
  Nothing is equipped and the player's real equipment is never modified.
  - Item keys: Ice Axe (adze) `1990000`, Ice Axe (hammer) `1990001`, Crampons `1990002`.
- **Gear scales per item.**
  - One axe → single-hand plant/hold clip, axe prop in that hand.
  - Both axes → dual plant/hold clip, a prop in each hand.
  - Crampons → crampon boot visual and foot benefit, independently of the axes.
- **Props only while climbing.** Axes appear in hand when a climb starts and are
  hidden again when it ends; they are visual props, not drawn weapons.
- **Less stamina drain with more gear.** Climb-skill stamina costs are scaled by
  INI multipliers that stack per item present (one axe < two axes; crampons add
  on top). Only climb skills are affected — no global stat changes.
- **Rope and anchors are animation-only.** No rope items. Resting on the rope
  (authored rope-rest clips) slowly recovers stamina, slower than on the ground,
  at an INI-tunable rate.
- **Fail closed.** The runtime plugin only acts on game builds it has verified
  (PE identity + RTTI). Anything else: it logs and does nothing.

## Repository layout

| Path | Contents |
|------|----------|
| `plugin/` | `ClimbmodASI` runtime plugin (C++ ASI, cross-built with zig), INI, tests |
| `tools/` | Python build/authoring scripts (packaging, mesh/texture/animation authoring) |
| `assets/` | Original authored assets only (meshes, textures, icons) |
| `docs/` | Design, research notes, routing/blocker findings |

## What is never committed

This is a public repository. It contains **no game files or game-derived data**:
no game executables, archives (`.paz/.pamt/.pac/.pab/.paa` originals), extracted
tables, skeleton/rig exports, crash dumps, or third-party mod binaries. Build
inputs that come from the game are read from your own install at build time.
See `docs/LEGAL.md`.

## Status

Work in progress. See `docs/` and open pull requests.

## License

MIT — see `LICENSE`. Parts of the plugin follow
[Stamina Master](https://github.com/shin2344234/stamina-master) (MIT); see
`THIRD_PARTY_NOTICES.md`.
