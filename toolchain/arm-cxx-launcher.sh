#!/usr/bin/env bash
# Miyoo gcc-8.3 + CMake 3.13 workaround: CMake can redundantly add the
# ARM sysroot's generic /usr/include with -isystem. That moves it before
# libstdc++'s #include_next search and breaks <cstdlib> / <stdlib.h>.
# Strip EXACTLY that directory; retain /usr/include/freetype2 and others.
set -euo pipefail
bad="${MIYOO_SYSROOT:-/opt/miyoomini-toolchain/arm-linux-gnueabihf/libc}/usr/include"
cmd=( )
while (($#)); do
    if [[ "$1" == "-isystem" && ${2-} == "$bad" ]]; then
        shift 2
        continue
    fi
    if [[ "$1" == "-isystem${bad}" ]]; then
        shift
        continue
    fi
    cmd+=("$1")
    shift
done
exec "${cmd[@]}"
