#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

if ! command -v python3 >/dev/null 2>&1; then
    echo "manifest: python3 is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-manifest.XXXXXX")
trap 'rm -rf "${work}"' EXIT

fail() {
    echo "manifest: $*" >&2
    exit 1
}

mkdir -p "${work}/dist"
for name in \
    husk-0.2.0-linux-x86_64 \
    husk-0.2.0-linux-aarch64 \
    husk-0.2.0-macos-universal2 \
    husk-0.2.0-windows-x86_64.exe \
    bamhusk-0.2.0-src.tar.gz; do
    printf 'fixture %s\n' "${name}" >"${work}/dist/${name}"
done

python3 "${root}/scripts/make-manifest.py" \
    --dir "${work}/dist" --version 0.2.0 --require-all

python3 - "${work}/dist" <<'PY'
import json
import sys
from pathlib import Path

directory = Path(sys.argv[1])
manifest = json.loads((directory / "manifest.json").read_text())
assert manifest["schema"] == 1
assert manifest["tool"] == "husk"
assert manifest["version"] == "0.2.0"
assert manifest["source"].endswith("bamhusk-0.2.0-src.tar.gz")
assert len(manifest["assets"]) == 4
assert {asset["os"] for asset in manifest["assets"]} == {
    "linux", "macos", "windows"
}
assert {asset["arch"] for asset in manifest["assets"]} == {
    "x86_64", "aarch64", "universal2"
}
for asset in manifest["assets"]:
    assert len(asset["sha256"]) == 64
    assert asset["size"] > 0
PY

python3 "${root}/scripts/make-manifest.py" --dir "${work}/dist" --verify

if python3 -c 'import jsonschema' >/dev/null 2>&1; then
    python3 "${root}/scripts/validate-manifest.py" \
        "${work}/dist/manifest.json"
fi

# Regeneration is deterministic.
cp "${work}/dist/manifest.json" "${work}/manifest.first"
python3 "${root}/scripts/make-manifest.py" \
    --dir "${work}/dist" --version 0.2.0 --require-all
cmp "${work}/manifest.first" "${work}/dist/manifest.json" ||
    fail "manifest generation is not deterministic"

# A missing platform is rejected when --require-all is used.
rm "${work}/dist/husk-0.2.0-linux-aarch64"
if python3 "${root}/scripts/make-manifest.py" \
    --dir "${work}/dist" --version 0.2.0 --require-all 2>/dev/null; then
    fail "missing platform was accepted"
fi

echo "manifest: all checks passed"
