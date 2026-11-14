#!/usr/bin/env bash
# Build an already configured Poppler 0.84.0 using the Miyoo ARM toolchain.
set -euo pipefail
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SRC="${SRC:-$PROJECT_ROOT/poppler-0.84.0}"
BUILD="${BUILD:-$SRC/build-miyoo-fast}"
[[ -f "$BUILD/CMakeCache.txt" ]] || { echo "Configure Poppler first (missing $BUILD/CMakeCache.txt)" >&2; exit 1; }
cmake --build "$BUILD" --target poppler --parallel 2
SO="$BUILD/libpoppler.so.94"
[[ -f "$SO" ]] || { echo "Expected output missing: $SO" >&2; exit 1; }
file "$SO"
readelf -d "$SO" | grep -E 'SONAME|NEEDED|RPATH|RUNPATH' || true
