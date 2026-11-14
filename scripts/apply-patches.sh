#!/usr/bin/env bash
# Bootstrap ONLY: apply patch series to matching, pristine Green and Poppler sources.
# Do not run after cloning the complete patched-source repository.
# Test the complete series on temporary copies before modifying source files.
set -euo pipefail

if [[ $# != 1 || ( "$1" != --check && "$1" != --apply ) ]]; then
  echo "Usage: bash scripts/apply-patches.sh --check|--apply" >&2
  exit 2
fi
ACTION="$1"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
GREEN_ROOT="${GREEN_ROOT:-$PROJECT_ROOT/green}"
POPLER_ROOT="${POPLER_ROOT:-$PROJECT_ROOT/poppler-0.84.0}"
KIT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for file in Makefile main.c green.c green.h sdl.c; do
  [[ -f "$GREEN_ROOT/$file" ]] || { echo "Missing original Green file: $GREEN_ROOT/$file" >&2; exit 1; }
done
[[ -f "$POPLER_ROOT/poppler/JPEG2000Stream.cc" ]] || {
  echo "Missing pristine Poppler 0.84.0 source: $POPLER_ROOT" >&2; exit 1;
}

GREEN_PATCHES=(
  001-fix-fit-page-reference.patch
  002-free-document-resources.patch
  003-safe-framebuffer-copy.patch
  004-reuse-poppler-page.patch
  005-event-driven-idle-timer.patch
  006-bounded-two-page-cache.patch
  007-miyoo-mini-keymap.patch
  008-multi-page-lru-cache.patch
  009-bottom-right-busy-indicator.patch
  010-clear-background-before-busy.patch
  011-black-background.patch
  012-miyoo-framebuffer-180.patch
)
POPLER_PATCH="$KIT/patches/poppler/001-jpx-reduced-decode-and-debug.patch"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/green" "$TMP/poppler/poppler"
cp "$GREEN_ROOT"/{Makefile,main.c,green.c,green.h,sdl.c} "$TMP/green/"
cp "$POPLER_ROOT/poppler/JPEG2000Stream.cc" "$TMP/poppler/poppler/"

apply_green() {
  local dir="$1" name
  for name in "${GREEN_PATCHES[@]}"; do
    echo "${2}: Green/$name"
    patch -s --batch --forward --fuzz=0 -p1 -d "$dir" < "$KIT/patches/green/$name"
  done
}

# Preflight the full sequence against copies before any real source changes.
apply_green "$TMP/green" Checking
patch -s --batch --forward --fuzz=0 -p1 -d "$TMP/poppler" < "$POPLER_PATCH"
echo 'All patches verified with zero fuzz.'
if [[ "$ACTION" == --check ]]; then
  echo 'No source files changed.'
  exit 0
fi

apply_green "$GREEN_ROOT" Applying
echo 'Applying: Poppler/001-jpx-reduced-decode-and-debug.patch'
patch -s --batch --forward --fuzz=0 -p1 -d "$POPLER_ROOT" < "$POPLER_PATCH"
echo 'Done: Green and Poppler patched.'
