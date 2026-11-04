#!/usr/bin/env python3
"""Build or verify the Husk release manifest and checksums."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ASSET_PATTERN = re.compile(
    r"^husk-(?P<version>[0-9]+\.[0-9]+\.[0-9]+)-"
    r"(?P<platform>linux-x86_64|linux-aarch64|macos-universal2|"
    r"windows-x86_64\.exe)$"
)

PLATFORMS = {
    "linux-x86_64": ("linux", "x86_64"),
    "linux-aarch64": ("linux", "aarch64"),
    "macos-universal2": ("macos", "universal2"),
    "windows-x86_64.exe": ("windows", "x86_64"),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def collect_assets(directory: Path, version: str) -> list[dict[str, object]]:
    assets: list[dict[str, object]] = []
    for path in sorted(directory.iterdir()):
        if not path.is_file():
            continue
        match = ASSET_PATTERN.match(path.name)
        if not match or match.group("version") != version:
            continue
        platform = match.group("platform")
        os_name, arch = PLATFORMS[platform]
        assets.append(
            {
                "os": os_name,
                "arch": arch,
                "file": path.name,
                "sha256": sha256(path),
                "size": path.stat().st_size,
            }
        )
    return assets


def checksum_lines(directory: Path, names: list[str]) -> str:
    lines = []
    for name in sorted(names):
        path = directory / name
        lines.append(f"{sha256(path)}  {name}")
    return "\n".join(lines) + "\n"


def build_manifest(directory: Path, version: str, base_url: str) -> dict:
    assets = collect_assets(directory, version)
    manifest = {
        "schema": 1,
        "tool": "husk",
        "version": version,
        "source": f"{base_url}/bamhusk-{version}-src.tar.gz",
        "assets": [
            {
                "os": asset["os"],
                "arch": asset["arch"],
                "url": f"{base_url}/{asset['file']}",
                "sha256": asset["sha256"],
                "size": asset["size"],
            }
            for asset in assets
        ],
    }
    return manifest


def write_outputs(
    directory: Path,
    manifest: dict,
    require_all: bool,
) -> None:
    version = manifest["version"]
    assets = collect_assets(directory, version)
    present = {
        f"{asset['os']}-{asset['arch']}" for asset in assets
    }
    expected = {
        f"{os_name}-{arch}" for os_name, arch in PLATFORMS.values()
    }
    if require_all and present != expected:
        missing = ", ".join(sorted(expected - present))
        raise SystemExit(f"missing release assets: {missing}")
    if not assets:
        raise SystemExit("no release assets found")

    for asset in assets:
        (directory / f"{asset['file']}.sha256").write_text(
            f"{asset['sha256']}  {asset['file']}\n", encoding="utf-8"
        )

    source = f"bamhusk-{version}-src.tar.gz"
    names = [str(asset["file"]) for asset in assets]
    if (directory / source).is_file():
        names.append(source)
        (directory / f"{source}.sha256").write_text(
            f"{sha256(directory / source)}  {source}\n", encoding="utf-8"
        )
    (directory / "SHA256SUMS").write_text(
        checksum_lines(directory, names), encoding="utf-8"
    )
    (directory / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


def verify(directory: Path) -> None:
    manifest_path = directory / "manifest.json"
    if not manifest_path.is_file():
        raise SystemExit("manifest.json is missing")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        raise SystemExit("unsupported manifest schema")
    if manifest.get("tool") != "husk":
        raise SystemExit("manifest tool is not husk")

    listed: set[str] = set()
    for asset in manifest.get("assets", []):
        name = str(asset.get("url", "")).rsplit("/", 1)[-1]
        path = directory / name
        if not path.is_file():
            raise SystemExit(f"missing asset: {name}")
        listed.add(name)
        if sha256(path) != asset.get("sha256"):
            raise SystemExit(f"checksum mismatch: {name}")
        if path.stat().st_size != asset.get("size"):
            raise SystemExit(f"size mismatch: {name}")

    sums_path = directory / "SHA256SUMS"
    if not sums_path.is_file():
        raise SystemExit("SHA256SUMS is missing")
    sums: dict[str, str] = {}
    for line in sums_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        digest, name = line.split("  ", 1)
        sums[name] = digest
    for name in listed:
        if name not in sums:
            raise SystemExit(f"{name} is missing from SHA256SUMS")
        if sums[name] != sha256(directory / name):
            raise SystemExit(f"SHA256SUMS mismatch: {name}")
    source_name = str(manifest.get("source", "")).rsplit("/", 1)[-1]
    if source_name:
        if not (directory / source_name).is_file():
            raise SystemExit(f"missing source tarball: {source_name}")
        if source_name not in sums:
            raise SystemExit(f"{source_name} is missing from SHA256SUMS")
        if sums[source_name] != sha256(directory / source_name):
            raise SystemExit(f"SHA256SUMS mismatch: {source_name}")
    print(f"verified {len(listed)} release asset(s)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", type=Path, default=Path("dist"))
    parser.add_argument("--version", required=False)
    parser.add_argument("--repo", default="alermol/bamhusk")
    parser.add_argument("--base-url")
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    directory: Path = args.dir
    if not directory.is_dir():
        raise SystemExit(f"not a directory: {directory}")

    if args.verify:
        verify(directory)
        return 0

    if not args.version:
        raise SystemExit("--version is required when building a manifest")
    base_url = args.base_url or (
        f"https://github.com/{args.repo}/releases/download/v{args.version}"
    )
    manifest = build_manifest(directory, args.version, base_url.rstrip("/"))
    write_outputs(directory, manifest, args.require_all)
    print(f"wrote manifest for {len(manifest['assets'])} asset(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
