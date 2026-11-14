#!/usr/bin/env python3
"""Public-repo guard: fail if tracked files look like game files or leak local paths.

Run from the repo root: python3 tools/check_no_game_files.py
"""
import pathlib, re, subprocess, sys

FORBIDDEN_EXT = {".exe", ".dll", ".dmp", ".paz", ".pamt", ".pac", ".pab", ".pabgb", ".pabgh",
                 ".paac", ".paa", ".pami", ".staticinfobody", ".asi", ".addon64"}
MAGIC = {b"MZ": "PE executable", b"MDMP": "minidump"}
PATH_LEAK = re.compile(rb"C:\\\\?Users\\\\?[A-Za-z0-9_.-]+|/home/[a-z0-9_-]+/|steamapps[\\/]+common", re.I)
TEXT_EXT = {".py", ".md", ".txt", ".json", ".cpp", ".h", ".S", ".sh", ".ini", ".yml", ".yaml", ".toml"}
MAX_PLAIN = 2 * 1024 * 1024  # anything bigger must be in LFS

files = subprocess.run(["git", "ls-files", "-z"], capture_output=True, check=True).stdout.split(b"\0")
lfs = set(subprocess.run(["git", "lfs", "ls-files", "-n"], capture_output=True, text=True).stdout.split())
errors = []
for raw in filter(None, files):
    name = raw.decode()
    p = pathlib.Path(name)
    if p.suffix.lower() in FORBIDDEN_EXT:
        errors.append(f"{name}: forbidden file type")
        continue
    if name in lfs or not p.is_file():
        continue
    data = p.read_bytes()
    for magic, what in MAGIC.items():
        if data.startswith(magic):
            errors.append(f"{name}: looks like a {what}")
    if len(data) > MAX_PLAIN:
        errors.append(f"{name}: {len(data)} bytes outside Git LFS")
    if p.suffix in TEXT_EXT and p.name != "check_no_game_files.py" and PATH_LEAK.search(data):
        errors.append(f"{name}: contains an absolute local path")
for e in errors:
    print("ERROR", e)
print(f"checked {len([f for f in files if f])} files, {len(errors)} problem(s)")
sys.exit(1 if errors else 0)
