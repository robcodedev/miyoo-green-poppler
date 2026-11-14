#!/usr/bin/env bash
# Install/check the host-side tools needed before downloading or building.
# Does not install any ARM libraries or modify the Miyoo cross-toolchain.
set -euo pipefail

if [[ $# -ne 1 || ( "$1" != --check && "$1" != --install ) ]]; then
  echo "Usage: bash scripts/prepare-host.sh --check|--install" >&2
  exit 2
fi

# command:apt-package (packages are for Debian/Ubuntu build HOST only)
TOOLS=(
  bash:bash curl:curl zstd:zstd ar:binutils readelf:binutils
  nm:binutils tar:tar xz:xz-utils gzip:gzip dpkg-deb:dpkg
  python3:python3 patch:patch make:make cmake:cmake file:file
  sha256sum:coreutils unzip:unzip pkg-config:pkg-config
)
missing_packages=()
missing_tools=()
for entry in "${TOOLS[@]}"; do
  command="${entry%%:*}"
  package="${entry#*:}"
  if ! command -v "$command" >/dev/null 2>&1; then
    missing_tools+=("$command")
    if [[ ! " ${missing_packages[*]} " == *" $package "* ]]; then
      missing_packages+=("$package")
    fi
  fi
done

if (( ${#missing_packages[@]} == 0 )); then
  echo 'Host prerequisites: OK (curl, zstd, ar, tar, cmake and other required tools found).'
  exit 0
fi

printf 'Missing host tools: %s\n' "${missing_tools[*]}" >&2
printf 'Required apt packages: %s\n' "${missing_packages[*]}" >&2

if [[ "$1" == --check ]]; then
  echo 'Install once with: bash scripts/prepare-host.sh --install' >&2
  exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo 'Automatic installation requires apt-get (Debian/Ubuntu); install the named packages manually.' >&2
  exit 1
fi
if (( EUID == 0 )); then
  apt-get update
  apt-get install -y "${missing_packages[@]}"
else
  if ! command -v sudo >/dev/null 2>&1; then
    echo 'Run as root or install sudo, then retry.' >&2
    exit 1
  fi
  sudo apt-get update
  sudo apt-get install -y "${missing_packages[@]}"
fi
exec bash "$0" --check
