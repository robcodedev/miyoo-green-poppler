#!/usr/bin/env bash
# Compare rebuilt Poppler shared-library compatibility. Doesn't install anything.
set -euo pipefail
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ROOT="${ROOT:-/root/workspace/RedOnion}"
SRC="${SRC:-$PROJECT_ROOT/poppler-0.84.0}"
BUILD="${BUILD:-$SRC/build-miyoo-fast}"
TC="${TC:-/opt/miyoomini-toolchain}"
ORIG="${ORIG:-$ROOT/static/build/.tmp_update/lib/libpoppler.so.94}"
NEW="${NEW:-$BUILD/libpoppler.so.94}"
[[ -f "$ORIG" && -f "$NEW" ]] || { echo "Missing library: original=$ORIG rebuilt=$NEW" >&2; exit 1; }
NM="$TC/usr/bin/arm-linux-gnueabihf-nm"
[[ -x "$NM" ]] || NM="$(command -v arm-linux-gnueabihf-nm || true)"
[[ -n "$NM" ]] || { echo 'Missing ARM nm' >&2; exit 1; }
DIR="$(mktemp -d)"; trap 'rm -rf "$DIR"' EXIT
echo '=== ORIGINAL ==='; file "$ORIG"; readelf -d "$ORIG" | grep -E 'SONAME|NEEDED|RPATH|RUNPATH'
echo '=== REBUILT ==='; file "$NEW"; readelf -d "$NEW" | grep -E 'SONAME|NEEDED|RPATH|RUNPATH'
echo '=== ORIGINAL EXPORTED SYMBOLS ABSENT FROM REBUILT LIBRARY ==='
"$NM" -D --defined-only "$ORIG" | awk '{print $3}' | sort -u > "$DIR/old"
"$NM" -D --defined-only "$NEW" | awk '{print $3}' | sort -u > "$DIR/new"
comm -23 "$DIR/old" "$DIR/new" > "$DIR/missing"
if [[ -s "$DIR/missing" ]]; then
  cat "$DIR/missing"
  echo 'WARNING: old exports missing; investigate before deploying.'
else
  echo '(none)'
fi
if readelf -d "$NEW" | grep -Eq 'RPATH|RUNPATH'; then
  echo 'WARNING: unexpected RPATH/RUNPATH; rerun CMake with CMAKE_SKIP_RPATH=ON' >&2
fi
echo 'NOTE: matching dependencies/exports do not guarantee ABI or rendering compatibility.'
