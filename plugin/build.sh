#!/usr/bin/env bash
# Cross-builds ClimbmodASI.asi on Linux/macOS with zig's clang (pip install ziglang).
# No MSVC needed; the result is a self-contained x64 Windows DLL.
set -euo pipefail
cd "$(dirname "$0")"
ZIG="${ZIG:-python3 -m ziglang}"
rm -rf dist obj && mkdir -p dist obj
CXXFLAGS="-target x86_64-windows-gnu -std=c++17 -O2 -fno-exceptions -fno-rtti -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE -Wall -Wno-unused-function"
for f in dllmain climbdiag mem log; do $ZIG c++ $CXXFLAGS -c src/$f.cpp -o obj/$f.o; done
$ZIG cc -target x86_64-windows-gnu -c src/thunks.S -o obj/thunks.o
$ZIG c++ -target x86_64-windows-gnu -shared -o dist/ClimbmodASI.asi obj/*.o -lkernel32 -s
rm -f dist/*.lib dist/*.pdb
cp ClimbmodASI.ini dist/
echo "built: dist/ClimbmodASI.asi"
