"""Post-build checks on dist/ClimbmodASI.asi: x64 DLL, minimal imports, version string."""
import pathlib, re, sys
import pefile

ROOT = pathlib.Path(__file__).resolve().parents[1]
dll = ROOT / "dist" / "ClimbmodASI.asi"
pe = pefile.PE(str(dll))
assert pe.FILE_HEADER.Machine == 0x8664, "not x64"
assert pe.FILE_HEADER.Characteristics & 0x2000, "not a DLL"
imports = sorted(e.dll.decode().lower() for e in pe.DIRECTORY_ENTRY_IMPORT)
allowed = re.compile(r"^(kernel32\.dll|api-ms-win-crt-[a-z]+-l1-1-0\.dll)$")
bad = [i for i in imports if not allowed.match(i)]
assert not bad, f"unexpected imports: {bad}"
ver = re.search(r'kVersion = "([^"]+)"', (ROOT / "src" / "build_info.h").read_text()).group(1)
assert ver.encode() in dll.read_bytes(), f"version {ver} not embedded"
print(f"OK {dll.name} {dll.stat().st_size} bytes, version {ver}, imports {imports}")
