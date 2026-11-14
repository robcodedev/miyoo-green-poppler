#!/usr/bin/env python3
"""Extract Ubuntu 22.04 (jammy) ARMHF *development files* into a local Miyoo build tree.

Downloads official Ubuntu Ports package indexes via HTTPS, selects armhf .deb
archives, validates SHA256 from the indexes, and extracts with dpkg-deb or an ar/zstd/tar fallback for older Debian.
No apt configuration changes, installation, or host compilation.
"""
import argparse
import hashlib
import lzma
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

BASE = "https://ports.ubuntu.com/ubuntu-ports/"
PACKAGES = {
    "libjpeg9-dev": "universe",
    "libtiff-dev": "main",
    "liblcms2-dev": "main",
    "libcurl4-openssl-dev": "main",
    "libpng-dev": "main",
}

GREEN_PACKAGES = {
    "libcairo2-dev": "main",
    "libglib2.0-dev": "main",
    "libpoppler-glib-dev": "main",
}


def download(url, path):
    if path.exists() and path.stat().st_size > 0:
        print(f"Cached: {path.name}")
        return
    print(f"Downloading: {url}", flush=True)
    temp = path.with_name(path.name + ".part")
    try:
        with urllib.request.urlopen(url, timeout=90) as src, temp.open("wb") as dst:
            while True:
                block = src.read(1024 * 1024)
                if not block:
                    break
                dst.write(block)
        temp.replace(path)
    finally:
        if temp.exists():
            temp.unlink()


def parse_index(index, names):
    results = {}
    with lzma.open(index, "rt", encoding="utf-8", errors="replace") as fh:
        fields = {}
        for line in fh:
            if line == "\n":
                name = fields.get("Package", "")
                if name in names and fields.get("Architecture") == "armhf":
                    results[name] = fields.copy()
                fields.clear()
                continue
            if line[:1] in (" ", "\t"):
                continue
            if ": " in line:
                key, val = line.rstrip("\n").split(": ", 1)
                if key in ("Package", "Architecture", "Version", "Filename", "SHA256"):
                    fields[key] = val
        name = fields.get("Package", "")
        if name in names and fields.get("Architecture") == "armhf":
            results[name] = fields.copy()
    return results


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()



def extract_armhf_deb(archive, stage):
    """Extract .deb data without needing a recent dpkg-deb on Debian Buster.

    Packages with zstd-compressed control or data members defeat dpkg-deb
    in older containers. Read just the data member using ar instead.
    """
    members = subprocess.check_output(
        ["ar", "t", str(archive)], universal_newlines=True
    ).splitlines()
    data_members = [member for member in members if member.startswith("data.tar")]
    if len(data_members) != 1:
        raise RuntimeError("Expected exactly one data.tar member in " + str(archive))

    member = data_members[0]
    if not any(name.endswith(".zst") for name in members):
        subprocess.run(["dpkg-deb", "-x", str(archive), str(stage)], check=True)
        return

    if member.endswith(".zst"):
        decoder = "zstd -dc"
        required = "zstd"
    elif member.endswith(".xz"):
        decoder = "xz -dc"
        required = "xz"
    elif member.endswith(".gz"):
        decoder = "gzip -dc"
        required = "gzip"
    elif member == "data.tar":
        decoder = "cat"
        required = "cat"
    else:
        raise RuntimeError("Unsupported Debian data archive: " + member)

    if not shutil_which("ar") or not shutil_which("tar") or not shutil_which(required):
        raise RuntimeError(
            "Missing extractor: need ar, tar, and " + required +
            "; for Zstandard run apt-get install -y zstd"
        )

    print("Using ar + " + required + " + tar for " + archive.name, flush=True)
    subprocess.run(
        ["bash", "-o", "pipefail", "-c",
         'ar p "$1" "$2" | ' + decoder + ' | tar -xf - -C "$3"',
         "bash", str(archive), member, str(stage)],
        check=True
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stage", type=Path, default=Path(os.environ.get("HDR", str(Path(__file__).resolve().parent.parent / "build-deps" / "armhf"))))
    ap.add_argument("--with-green", action="store_true", help="Also stage public ARMHF Cairo, GLib and Poppler-GLib headers for Green (verify against the shipped runtimes)")
    args = ap.parse_args()
    packages = dict(PACKAGES)
    if args.with_green:
        packages.update(GREEN_PACKAGES)
    # Fail before the first download if old Debian lacks curl/zstd/ar/etc.
    checker = Path(__file__).resolve().parent / "prepare-host.sh"
    if subprocess.run(["bash", str(checker), "--check"]).returncode != 0:
        sys.exit("Error: required host tools are missing; run bash scripts/prepare-host.sh --install")
    stage = args.stage.resolve()
    cache = stage / "_ubuntu_jammy_armhf_downloads"
    cache.mkdir(parents=True, exist_ok=True)
    print(f"Staging ARM development headers at: {stage}")
    if not shutil_which("dpkg-deb"):
        sys.exit("Error: dpkg-deb is required")
    entries = {}
    for component in sorted(set(packages.values())):
        index = cache / f"jammy-{component}-armhf-Packages.xz"
        download(BASE + f"dists/jammy/{component}/binary-armhf/Packages.xz", index)
        subset = {k for k, v in packages.items() if v == component}
        entries.update(parse_index(index, subset))
    missing = set(packages) - set(entries)
    if missing:
        sys.exit("Error: packages not in indexes: " + ", ".join(sorted(missing)))
    for name in packages:
        p = entries[name]
        rel = p["Filename"]
        expected = p["SHA256"].lower()
        dest = cache / Path(rel).name
        download(BASE + rel, dest)
        if sha256_file(dest).lower() != expected:
            if dest.exists():
                dest.unlink()
            sys.exit(f"SHA256 mismatch for {name}; stopped without extraction")
        print(f"Extracting {name} {p['Version']} (ARMHF) -> {stage}", flush=True)
        extract_armhf_deb(dest, stage)
    print("\nDone. Only .deb contents were extracted; no host packages installed.")
    print("Next: run the kit scripts/audit-arm-deps.sh and scripts/configure-poppler-arm.sh")
    print("NOTE: Ubuntu Jammy development headers are provisional with RedOnion runtimes; check ABI and test the device.")


def shutil_which(name):
    from shutil import which
    return which(name)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit(f"Error: {exc}")
