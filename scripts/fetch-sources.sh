#!/usr/bin/env bash
# Fetch pristine Poppler 0.84.0 and OpenJPEG 2.4.0 source archives.
# For bootstrap use only. Skip if the repository already contains patched sources.
set -euo pipefail
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/scripts/prepare-host.sh" --check
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fetch() {
  local url="$1" target="$2" archive="$3" expected="$4"
  if [[ -e "$PROJECT_ROOT/$target" ]]; then
    [[ -f "$PROJECT_ROOT/$target/$expected" ]] || {
      echo "ERROR: $target exists but is not the expected source tree" >&2; exit 1;
    }
    echo "Already present: $target"
    return
  fi
  echo "Fetching $target"
  curl --fail --location --retry 3 --output "$TMP/$archive" "$url"
  tar -xf "$TMP/$archive" -C "$PROJECT_ROOT"
  [[ -f "$PROJECT_ROOT/$target/$expected" ]] || {
    echo "ERROR: extracted source not found in $target" >&2; exit 1;
  }
}
fetch 'https://poppler.freedesktop.org/poppler-0.84.0.tar.xz' \
  'poppler-0.84.0' 'poppler-0.84.0.tar.xz' 'poppler/JPEG2000Stream.cc'
fetch 'https://github.com/uclouvain/openjpeg/archive/refs/tags/v2.4.0.tar.gz' \
  'openjpeg-2.4.0' 'openjpeg-2.4.0.tar.gz' 'CMakeLists.txt'
echo 'Sources ready. Apply patches ONLY if using a pristine bootstrap tree; not on a cloned, patched source repository.'
