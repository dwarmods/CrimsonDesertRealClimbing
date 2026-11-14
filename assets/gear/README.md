# Climbing mod – gear assets (v1.1)

Old-school wooden-shaft piolets and a hinged 12-point bent-bar crampon, built procedurally in Blender 4.2 (headless).
Units: metres. Ice axes: Z-up, spike at origin. Crampon: X forward, sole at z=0, fits a ~28 cm boot.

## Models (`models/`) – OBJ+MTL / FBX / .blend, triangulated, UV-unwrapped (smart UV)
| file | tris | size (m) |
|---|---|---|
| piolet_adze.*         | 4,204 | 0.37 × 0.06 × 0.62 |
| piolet_hammer.*       | 3,928 | 0.29 × 0.04 × 0.62 |
| crampon_hinged_12pt.* | 4,460 | 0.34 × 0.105 × 0.16 (left; mirror Y for right) |

## Textures (`textures/`) – 2048², one set per model
- `<name>_albedo.png/.dds` – base colour, sRGB (DDS: BC1/DXT1)
- `<name>_normal.png/.dds` – tangent-space normal, OpenGL (+Y) (DDS: BC5)
- `<name>_orm.png/.dds` – R=AO, G=roughness, B=metallic, linear (DDS: BC1)
DDS files here have no mipmaps (written with Pillow). For the final game build, re-encode the PNGs with
texconv to match the game's formats, e.g. BC7_UNORM albedo, BC5_UNORM normal, with full mips.
The game's own material layout (which maps go into which slot) must be copied from the base item's .pac_xml.

## Previews (`previews/`)
piolet_adze.png, piolet_hammer.png, crampon_hinged_12pt.png (procedural shaders), full_set.png (baked textures only).

## Scripts (`scripts/`)
- `build.py` – builds all models and previews (`blender -b -P scripts/build.py`, ~2 min)
- `bake.py` – bakes the maps: `blender -b -P scripts/bake.py -- <name>` (RES env var, default 2048)
- `render_baked.py` – renders full_set.png from the baked maps
- `build_v0.py`, `build_v1.py` – older versions; old outputs are in `models/v0`, `previews/v0`
