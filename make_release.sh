#!/bin/sh
# Build completa con llvm-mingw / MinGW-w64 (anche da Linux):
#   ./make_release.sh [prefisso-toolchain]
# Produce in dist/: Stackly.exe, StacklySetup.exe, Stackly-<ver>-portable.zip
set -e
cd "$(dirname "$0")"
TC=${1:-x86_64-w64-mingw32-}
VER=$(sed -n 's/.*STACKLY_VERSION_STR *"\(.*\)".*/\1/p' src/resource.h)
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -municode -DUNICODE -D_UNICODE"
mkdir -p build dist installer/payload

# Tabella delle traduzioni: rigenerata e verificata (errore se manca una traduzione)
if command -v python3 >/dev/null 2>&1; then python3 tools/i18n.py; fi

${TC}windres --codepage=65001 -Isrc src/app.rc -O coff -o build/app.res.o
${TC}clang++ $CXXFLAGS src/main.cpp build/app.res.o -o build/Stackly.exe -mwindows -static -s \
  -lcomctl32 -lshell32 -lole32 -lwinhttp -lwindowscodecs -luuid -luxtheme -ldwmapi -lgdiplus

cp build/Stackly.exe installer/payload/Stackly.exe
${TC}windres --codepage=65001 -Iinstaller installer/setup.rc -O coff -o build/setup.res.o
${TC}clang++ $CXXFLAGS installer/setup.cpp build/setup.res.o -o build/StacklySetup.exe -mwindows -static -s \
  -lcomctl32 -lshell32 -lole32 -luuid -lshlwapi -lmsimg32

cp build/Stackly.exe dist/
cp build/StacklySetup.exe "dist/StacklySetup-$VER.exe"
rm -f "dist/Stackly-$VER-portable.zip"
(cd build && zip -q "../dist/Stackly-$VER-portable.zip" Stackly.exe)
echo "Fatto: dist/ (versione $VER)"
