# Offline reverse-engineering helpers (read-only)

These scripts work on a local copy of `CrimsonDesert.exe` that you supply. No game binary
or exe-derived dump is stored in this repo. Only the resulting offsets and similarity
scores (plain text in `docs/research/`) are committed.

Requirements: `pip install pefile capstone`. Run the scripts from this folder.

| Script | Inputs (env) | Output |
|---|---|---|
| `port3663.py` | `CD_EXE_3576`, `CD_EXE_3663` | `vtables_3663.json` |
| `port3732.py` | `CD_EXE_3663`, `CD_EXE_3732` | `port3732.json` (6 RTTI vtables + PointClimb slot similarity) |
| `port3732_layout.py` | same | `layout3732.json` (equip worker signature, component vtable similarity) |
| `verify_gate.py` | `CD_EXE_<build>` per BuildDef | `gate_replay.json`; prints `GATE_REPLAY_OK` when every BuildDef in `plugin/src/build_info.h` passes the plugin's gate |

`pe.py` is the shared PE/RTTI loader. Its exe path comes from `CD_EXE`.
