#!/usr/bin/env bash
# Build Green only for Miyoo using existing ARM headers and RedOnion ARM libs.
set -euo pipefail
# Source lives in this standalone repository; ARM runtime libraries remain in RedOnion.
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
GREEN_ROOT="${GREEN_ROOT:-$PROJECT_ROOT/green}"
ROOT="${ROOT:-/root/workspace/RedOnion}"
TC="${TC:-/opt/miyoomini-toolchain}"
LIB="${LIB:-$ROOT/static/build/.tmp_update/lib}"
PARA="$LIB/parasyte"
HDR="${HDR:-$PROJECT_ROOT/build-deps/armhf}"
cd "$GREEN_ROOT"
SDL_CONFIG="$TC/arm-linux-gnueabihf/libc/usr/bin/sdl-config"
for f in "$HDR/usr/include/poppler/glib/poppler.h" "$HDR/usr/include/cairo/cairo.h"          "$LIB/libpoppler-glib.so" "$PARA/libcairo.so.2"; do
    test -f "$f" || { echo "Missing: $f" >&2; exit 1; }
done
ARM_CFLAGS="-I$HDR/usr/include/poppler -I$HDR/usr/include/glib-2.0 -I$HDR/usr/lib/arm-linux-gnueabihf/glib-2.0/include -I$HDR/usr/include/cairo"
ARM_LIBS="-L$LIB -L$PARA -Wl,-rpath-link,$LIB -Wl,-rpath-link,$PARA -lpoppler-glib -l:libcairo.so.2 -l:libgobject-2.0.so.0 -l:libglib-2.0.so.0"
make clean
make MIYOO=1 CC="$TC/usr/bin/arm-linux-gnueabihf-gcc"     POPPLER_CFLAGS="$ARM_CFLAGS" POPPLER_LIBS="$ARM_LIBS"     SDL_CFLAGS="$("$SDL_CONFIG" --cflags)" SDL_LIBS="$("$SDL_CONFIG" --libs)"
file green
readelf -d green | grep NEEDED || true
