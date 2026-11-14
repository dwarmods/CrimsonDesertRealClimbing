# What is in this public repository, and what is deliberately not

Everything here is either original work or plain facts (offsets, hashes, RTTI names)
needed to verify a game build at runtime. Nothing from the game is redistributed.

## Included
- Plugin source (`plugin/`), MIT; Stamina Master (MIT) notice in `THIRD_PARTY_NOTICES.md`.
- Original, procedurally built meshes and their baked textures: two wooden-shaft piolets
  (adze, hammer), hinged 12-point crampon, oval steel carabiner, forged ring piton, hemp rope
  segment, bowline waist tie (`assets/`), icons and renders of those props only.
- Blender/Python scripts that generate those assets (`tools/gear`, `tools/rope`) and reverse-
  engineering helper scripts (`tools/re`) that read *your own* game exe at a path you supply.
- Research notes with RVAs, RTTI names, PE identity values and SHA-256 of the exes they were
  read from (`docs/research`).

## Excluded (keep local, regenerate from your own install)
| Item | Why | How to regenerate |
|---|---|---|
| `CrimsonDesert.exe` (and any chunk of it) | game binary | your install |
| Extracted tables (ItemInfo, StringInfo, StoreInfo, ItemGroupInfo, PartPrefabTable, ...) | game data | CDMW snapshot from your install |
| CDMW-built gear package (`0036/0.paz`, `0.pamt`, `manifest.json`, shops/groups json) | contains whole vanilla tables | rebuild with CDMW against your install |
| Hand-model variants of the axes | merged with vanilla hand/weapon prefab data | rebuild with CDMW |
| Authored climb clips `climbmod_piolet_{dual,left,right}_plant_hold_00.paa` + GLB/sample JSON | keyed on the game's 448-bone Kliff rig and keep native lower-body pose / battleaxe grip | authoring script needs the rig exported from your install (not shipped) |
| Rope slice clips (`climbmod_rope_*`.paa/.glb/samples) and their baked rope-prop motion GLBs, rope preview GIF/MP4/stills with the character | same rig dependency | same |
| Vanilla base-mesh exports, compare renders (`base_compare.png`, `crampon_on_boot.png`) | game meshes | CDMW export |
| Crash dumps, game/launcher logs, `user_engine_option_save.xml` | private + game data | — |
| Third-party mod binaries (RenoDX, LODFix, ReShade, other ASIs) | not ours | their authors |

`tools/check_no_game_files.py` runs in CI and fails on game file types, PE/minidump
magic, large non-LFS blobs and absolute local paths.
