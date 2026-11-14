#!/usr/bin/env bash
# Prepare and configure an ARM-only, full-feature Poppler 0.84.0 build.
# Requires matching ARM development headers in HDR; does not install host packages.
set -euo pipefail

PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ROOT="${ROOT:-/root/workspace/RedOnion}"  # Only needed for target ARM runtime libraries
SRC="${SRC:-$PROJECT_ROOT/poppler-0.84.0}"
TC="${TC:-/opt/miyoomini-toolchain}"
LIB="${LIB:-$ROOT/static/build/.tmp_update/lib}"
PARA="$LIB/parasyte"
HDR="${HDR:-$PROJECT_ROOT/build-deps/armhf}"
OPJ="${OPJ:-$PROJECT_ROOT/openjpeg-arm}"
SYSROOT="$TC/arm-linux-gnueabihf/libc"
STAGE="${STAGE:-$SRC/build-miyoo-deps}"
BUILD="${BUILD:-$SRC/build-miyoo-fast}"
KIT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KITROOT="$(cd "$KIT/.." && pwd)"
CMS="${CMS:-none}"
[[ "$CMS" == none || "$CMS" == lcms2 ]] || { echo "ERROR: CMS must be none or lcms2" >&2; exit 1; }

fatal() { echo "ERROR: $*" >&2; exit 1; }
[ -f "$SRC/CMakeLists.txt" ] || fatal "not a Poppler source directory: $SRC"
[ -x "$TC/usr/bin/arm-linux-gnueabihf-g++" ] || fatal 'Miyoo C++ compiler missing'
[ -x "$KITROOT/toolchain/arm-cxx-launcher.sh" ] || fatal 'C++ launcher missing or not executable'
[ ! -f "$BUILD/CMakeCache.txt" ] || fatal "Build directory is not fresh: $BUILD (choose another BUILD=...)"

# Stage linker names only; the actual versioned libraries stay in RedOnion.
mkdir -p "$STAGE/lib"
get_arm_so() {
  local soname="$1" root so arch
  for root in "$LIB" "$PARA" "$SYSROOT/usr/lib" "$SYSROOT/lib"; do
    so="$root/$soname"
    if [ -f "$so" ]; then
      arch="$(readelf -h "$so" 2>/dev/null | sed -n 's/.*Machine:[[:space:]]*//p' | head -1)"
      [ "$arch" = ARM ] || fatal "$so is not an ARM ELF ($arch)"
      printf '%s' "$so"
      return
    fi
  done
  fatal "missing ARM runtime library $soname; see ./audit-arm-deps.sh"
}
link_so() {
  local soname="$1" name="$2" target
  target="$(get_arm_so "$soname")"
  ln -sfn "$target" "$STAGE/lib/$name"
  printf 'link %-20s -> %s\n' "$name" "$target"
}
link_so libjpeg.so.9 libjpeg.so
link_so libtiff.so.5 libtiff.so
link_so liblcms2.so.2 liblcms2.so
link_so libcurl.so.4 libcurl.so
link_so libpng16.so.16 libpng16.so

find_arm_header() {
  local name="$1" root result
  for root in "$HDR/usr/include" "$SYSROOT/usr/include" "$OPJ/include"; do
    if [ -d "$root" ]; then
      result="$(find "$root" -type f -name "$name" -print -quit 2>/dev/null)"
      if [ -n "$result" ]; then printf '%s' "$result"; return; fi
    fi
  done
  fatal "missing ARM development header $name. Extract compatible ARM -dev files to $HDR."
}

JPEG_HEADER="$(find_arm_header jpeglib.h)"
JCONFIG_HEADER="$(find_arm_header jconfig.h)"
JMORECFG_HEADER="$(find_arm_header jmorecfg.h)"
TIFF_HEADER="$(find_arm_header tiffio.h)"
TIFFCONF_HEADER="$(find_arm_header tiffconf.h)"
LCMS_HEADER="$(find_arm_header lcms2.h)"
CURL_HEADER="$(find_arm_header curl.h)"
PNG_HEADER="$(find_arm_header png.h)"
PNGLIBCONF_HEADER="$(find_arm_header pnglibconf.h)"
CAIRO_HEADER="$(find_arm_header cairo.h)"

# Do not silently mix a staged ABI-specific header with its old toolchain counterpart.
for pair in "JPEG_HEADER:JCONFIG_HEADER" "JPEG_HEADER:JMORECFG_HEADER" "PNG_HEADER:PNGLIBCONF_HEADER" "TIFF_HEADER:TIFFCONF_HEADER"; do
  primary="${pair%%:*}"
  supporting="${pair##*:}"
  if [[ "${!primary}" == "$HDR/"* && "${!supporting}" != "$HDR/"* ]]; then
    fatal "${primary} is staged in $HDR but ${supporting} comes from elsewhere (${!supporting}); stage the complete matching development header set"
  fi
done

# Prevent two particularly easy ABI mismatches on this toolchain.
if ! grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+JPEG_LIB_VERSION[[:space:]]+90([[:space:]]|$)' "$JPEG_HEADER"; then
  fatal "jpeglib.h is not marked JPEG_LIB_VERSION 90 ($JPEG_HEADER). Do not link libjpeg62 headers to libjpeg.so.9."
fi
if ! grep -Eq 'PNG_LIBPNG_VER_STRING[[:space:]]+"1\.6\.' "$PNG_HEADER"; then
  fatal "png.h is not libpng 1.6 ($PNG_HEADER). The toolchain's libpng 1.2 headers are incompatible with libpng16.so.16."
fi

JPEG_INC="$(dirname "$JPEG_HEADER")"
TIFF_INC="$(dirname "$TIFF_HEADER")"
LCMS_INC="$(dirname "$LCMS_HEADER")"
CURL_INC="$(dirname "$(dirname "$CURL_HEADER")")"  # find_package(CURL) expects <curl/curl.h>
PNG_INC="$(dirname "$PNG_HEADER")"
CAIRO_INC="$(dirname "$CAIRO_HEADER")"

[ "$(basename "$(dirname "$CURL_HEADER")")" = curl ] || fatal "curl header is not in a curl/ directory: $CURL_HEADER"
[ -f "$TIFFCONF_HEADER" ] || fatal 'tiffconf.h missing'
[ -f "$PNGLIBCONF_HEADER" ] || fatal 'pnglibconf.h missing'

OPJ_CFG="$(find "$OPJ" -name OpenJPEGConfig.cmake -print -quit 2>/dev/null)"
[ -n "$OPJ_CFG" ] || fatal "OpenJPEGConfig.cmake missing from $OPJ"

# Keep pkg-config from silently selecting native x86_64 dependencies.
export PKG_CONFIG_LIBDIR="$HDR/usr/lib/arm-linux-gnueabihf/pkgconfig:$HDR/usr/lib/pkgconfig:$HDR/usr/share/pkgconfig"
unset PKG_CONFIG_SYSROOT_DIR || true

# Allow imported library dependencies to be found by the ARM linker.
LINKFLAGS="-L$STAGE/lib -L$LIB -L$PARA -Wl,-rpath-link,$STAGE/lib -Wl,-rpath-link,$LIB -Wl,-rpath-link,$PARA"

# Some Debian ARM dev headers include arch-specific generated headers.
ARCHINC=''
if [ -d "$HDR/usr/include/arm-linux-gnueabihf" ]; then
  ARCHINC="-I$HDR/usr/include/arm-linux-gnueabihf"
fi

printf "\nConfiguring ARM Poppler: CMS=%s (default none for Miyoo speed).\n" "$CMS"
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$KITROOT/toolchain/MiyooArmToolchain.cmake" \
  -DCMAKE_FIND_ROOT_PATH="$STAGE;$OPJ;$HDR;$SYSROOT" \
  -DCMAKE_PREFIX_PATH="$STAGE;$OPJ;$HDR;$SYSROOT" \
  -DCMAKE_C_FLAGS="$ARCHINC" \
  -DCMAKE_CXX_FLAGS="$ARCHINC" \
  -DCMAKE_SHARED_LINKER_FLAGS="$LINKFLAGS" \
  -DCMAKE_SKIP_RPATH=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DFONT_CONFIGURATION=generic \
  -DCAIRO_INCLUDE_DIRS="$CAIRO_INC" \
  -DCAIRO_LIBRARIES="$PARA/libcairo.so.2" \
  -DOpenJPEG_DIR="$(dirname "$OPJ_CFG")" \
  -DJPEG_INCLUDE_DIR="$JPEG_INC" \
  -DJPEG_LIBRARY="$STAGE/lib/libjpeg.so" \
  -DJPEG_LIBRARY_RELEASE="$STAGE/lib/libjpeg.so" \
  -DTIFF_INCLUDE_DIR="$TIFF_INC" \
  -DTIFF_LIBRARY="$STAGE/lib/libtiff.so" \
  -DTIFF_LIBRARY_RELEASE="$STAGE/lib/libtiff.so" \
  -DLCMS2_INCLUDE_DIR="$LCMS_INC" \
  -DLCMS2_LIBRARIES="$STAGE/lib/liblcms2.so" \
  -DCURL_INCLUDE_DIR="$CURL_INC" \
  -DCURL_LIBRARY="$STAGE/lib/libcurl.so" \
  -DCURL_LIBRARY_RELEASE="$STAGE/lib/libcurl.so" \
  -DPNG_PNG_INCLUDE_DIR="$PNG_INC" \
  -DPNG_LIBRARY_RELEASE="$STAGE/lib/libpng16.so" \
  -DENABLE_DCTDECODER=libjpeg \
  -DENABLE_LIBOPENJPEG=openjpeg2 \
  -DENABLE_CMS="$CMS" \
  -DENABLE_LIBTIFF=ON \
  -DENABLE_LIBCURL=ON \
  -DENABLE_ZLIB=ON \
  -DENABLE_GLIB=OFF \
  -DENABLE_QT5=OFF \
  -DENABLE_CPP=OFF \
  -DENABLE_UTILS=OFF \
  -DENABLE_NSS3=OFF \
  -DENABLE_GOBJECT_INTROSPECTION=OFF \
  -DBUILD_GTK_TESTS=OFF \
  -DBUILD_QT5_TESTS=OFF \
  "$@" | tee "$STAGE/configure.log"

printf '\n=== Detected features (CMS follows chosen profile) ===\n'
grep -E 'cairo output:|use libjpeg:|use libpng:|use libtiff:|use curl:|use libopenjpeg2:|use lcms2:' "$STAGE/configure.log" || true
printf '\nNext: cmake --build "%s" --target poppler --parallel 2\n' "$BUILD"
printf 'Check SONAME/NEEDED and exported C++ symbols before deploying to Miyoo.\n'
