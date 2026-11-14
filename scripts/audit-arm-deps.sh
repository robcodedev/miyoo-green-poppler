#!/usr/bin/env bash
# Read-only audit of the ARM runtime libraries and headers needed by Poppler 0.84.0.
set -u
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ROOT="${ROOT:-/root/workspace/RedOnion}"  # RedOnion is runtime-library supplier only
TC="${TC:-/opt/miyoomini-toolchain}"
LIB="${LIB:-$ROOT/static/build/.tmp_update/lib}"
HDR="${HDR:-$PROJECT_ROOT/build-deps/armhf}"
OPJ="${OPJ:-$PROJECT_ROOT/openjpeg-arm}"
SYSROOT="$TC/arm-linux-gnueabihf/libc"

printf 'RedOnion runtime: %s\nARM header staging: %s\nOpenJPEG staging: %s\n' "$LIB" "$HDR" "$OPJ"
printf '\n=== Shipped Poppler dependencies ===\n'
if [ -e "$LIB/libpoppler.so.94" ]; then
  readelf -d "$LIB/libpoppler.so.94" | grep -E 'NEEDED|SONAME' || true
else
  echo 'WARNING: RedOnion libpoppler.so.94 not found'
fi
printf '\n=== Required ARM shared libraries ===\n'
for so in libjpeg.so.9 libpng16.so.16 libtiff.so.5 liblcms2.so.2 libcurl.so.4 libopenjp2.so.7 libcairo.so.2; do
  found=''
  for dir in "$LIB" "$LIB/parasyte" "$SYSROOT/usr/lib" "$SYSROOT/lib"; do
    if [ -f "$dir/$so" ]; then found="$dir/$so"; break; fi
  done
  if [ -z "$found" ]; then
    printf 'MISSING %-22s\n' "$so"
  else
    arch="$(readelf -h "$found" 2>/dev/null | sed -n 's/.*Machine:[[:space:]]*//p' | head -1)"
    printf '%-22s %s  [%s]\n' "$so" "$found" "$arch"
    if [ "$arch" != ARM ]; then echo "ERROR: wrong architecture for $so"; fi
  fi
done
printf '\n=== Development headers (ARM staging or cross sysroot only) ===\n'
for head in jpeglib.h jconfig.h jmorecfg.h tiffio.h tiffconf.h lcms2.h curl.h png.h pnglibconf.h cairo.h openjpeg.h; do
  found=''
  for dir in "$HDR/usr/include" "$SYSROOT/usr/include" "$OPJ/include"; do
    if [ -d "$dir" ]; then
      found="$(find "$dir" -type f -name "$head" -print -quit 2>/dev/null)"
      [ -z "$found" ] || break
    fi
  done
  if [ -z "$found" ]; then printf 'MISSING %-18s\n' "$head"; else printf '%-18s %s\n' "$head" "$found"; fi
done
printf '\n=== OpenJPEG CMake package ===\n'
if [ -d "$OPJ" ]; then find "$OPJ" -name OpenJPEGConfig.cmake -print; else echo 'MISSING OpenJPEG staged installation'; fi
printf '\nNOTE: libjpeg.so.9 requires JPEG version-9-compatible headers.\n'
printf '      Do not use libjpeg62 headers from an unrelated Debian development package.\n'
