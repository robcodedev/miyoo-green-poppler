# Miyoo Green + Poppler

Optimized [Green PDF Reader](https://github.com/schandinat/green) and Poppler 0.84.0 for the Miyoo Mini / Mini Plus, built with the Miyoo ARM cross-toolchain. RedOnion provides the compatible ARM runtime libraries.

This repository contains the **modified Green and Poppler source trees** (`green/` and `poppler-0.84.0/`), build scripts, reference patches, and the Green shell launcher. The standalone DevKit ZIP includes the build files and patches **but not the source trees**; a complete source checkout is required for compilation.

## Requirements

- Linux build host (tested workflow: Debian container).
- Miyoo ARM cross-toolchain at `/opt/miyoomini-toolchain` (`TC` can override this path).
- RedOnion runtime libraries at `/root/workspace/RedOnion/static/build/.tmp_update/lib` (`ROOT` or `LIB` can override this path).
- Network access to obtain OpenJPEG 2.4.0 source and ARM development headers.

Prepare the host tools first. This step checks or installs tools such as `curl`, `zstd`, `ar`, CMake and Python; it does not install target ARM libraries into the host:

```bash
bash scripts/prepare-host.sh --check
bash scripts/prepare-host.sh --install   # only if the check reports missing tools
```

## Build

Run from the repository root:

```bash
# The checkout already includes modified Green and Poppler sources.
bash scripts/fetch-sources.sh            # obtains missing OpenJPEG source
python3 scripts/fetch-armhf-dev.py --with-green
bash scripts/build-openjpeg-arm.sh
bash scripts/audit-arm-deps.sh
chmod +x toolchain/arm-cxx-launcher.sh
bash scripts/configure-poppler-arm.sh
bash scripts/build-poppler-arm.sh
bash scripts/verify-poppler.sh
bash scripts/build-green-arm.sh
```

The default Poppler configuration enables JPEG, PNG, TIFF, Cairo, CURL and JPEG2000, but disables LCMS2 color management (`ENABLE_CMS=none`) to improve performance on the Miyoo. Disabling ICC color management can affect color fidelity; consult [build notes](docs/BUILD_NOTES.md) for details. CMake RPATH is disabled.

**Outputs:**

- `green/green` — compiled ARM executable (deploy as `green.bin`).
- `poppler-0.84.0/build-miyoo-fast/libpoppler.so.94` — compiled ARM shared library.
- `device/green` — shell launcher (deploy as `green`).

The modified sources already contain the changes represented by `patches/`. **Do not reapply those patches to a source checkout from this repository.** The patch files and `scripts/apply-patches.sh` are provided for examining or reproducing the modifications against compatible pristine source versions.

## Deploy with RedOnion

Use RedOnion's existing paths, not a separate library directory:

```text
<existing Green application directory>/
  green          # shell launcher, from device/green
  green.bin      # compiled Green executable

RedOnion/static/build/.tmp_update/lib/
  libpoppler.so.94
```

For example, with the actual Green application directory supplied as `GREEN_DIR`:

```bash
ROOT=/root/workspace/RedOnion
GREEN_DIR=/path/to/existing/green/application

# Preserve backups of existing files before replacing them.
install -m 644 poppler-0.84.0/build-miyoo-fast/libpoppler.so.94 \
  "$ROOT/static/build/.tmp_update/lib/libpoppler.so.94"
install -m 755 green/green "$GREEN_DIR/green.bin"
install -m 755 device/green "$GREEN_DIR/green"
```

The launcher configures JPEG2000 reduction and Green's page cache, and forwards all arguments to `green.bin`. There is no private `lib/` directory or `LD_LIBRARY_PATH` override. Green includes a black startup background, a three-dot busy indicator, and the Miyoo framebuffer 180° output correction.

## Implementation notes

- `GREEN_JPX_REDUCE=2` enables reduced JPEG2000 decoding for eligible images; it may trade some image sharpness for speed. It has no effect on ordinary JPEG images.
- `GREEN_CACHE_PAGES=5` and `GREEN_CACHE_MB=12` bound the recent-page cache.
- The optimized Poppler library preserves SONAME `libpoppler.so.94`. Check ABI dependencies and exported symbols before substituting a system-wide library; compatible SONAME alone does not prove behavioral compatibility.
- Further build and compatibility details: [docs/BUILD_NOTES.md](docs/BUILD_NOTES.md).

## Licenses

Green is GPL-3.0-licensed and Poppler has its own upstream license notices. Preserve the corresponding notices in the complete source trees and comply with applicable source-distribution obligations when distributing binaries. Third-party components retain their respective licenses.
