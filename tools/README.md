# Tools

- `gear/` — Blender 4.2 headless scripts that build and bake the piolets and crampon
  (`blender -b -P tools/gear/build.py`, then `bake.py -- <name>`). They write into a
  working root (`OUT` at the top of each script); set it to a local folder.
  `align.py` needs base meshes exported from your own install (not shipped).
- `rope/` — rope, piton, carabiner and waist-tie prop builders, bake, DDS/ORM packing.
- `re/` — read-only exe analysis used to port the plugin between game builds
  (`port3663*.py`, `verify_gate.py`). Point them at your own `CrimsonDesert.exe`
  copies (env `CD_EXE_OLD` / `CD_EXE_NEW`); requires `pefile` and `capstone`.
- `check_no_game_files.py` — public-repo guard, also run by CI.
