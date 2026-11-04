# Verification

Every binary release asset has a SHA-256 checksum in both `SHA256SUMS` and
`manifest.json`; the source tarball is listed in `SHA256SUMS`. The macOS
binary is signed and notarised when the release secrets are configured. The
source tarball is the fallback when a binary is unavailable or untrusted.

## Verify the checksums

Linux:

```sh
sha256sum -c SHA256SUMS
```

macOS:

```sh
shasum -a 256 -c SHA256SUMS
```

Windows PowerShell:

```powershell
Get-Content SHA256SUMS | ForEach-Object {
  $hash, $name = $_ -split '  ', 2
  $actual = (Get-FileHash $name -Algorithm SHA256).Hash.ToLower()
  if ($actual -ne $hash) { throw "checksum mismatch: $name" }
}
```

The manifest is self-contained:

```sh
python3 scripts/make-manifest.py --dir . --verify
```

## Verify the manifest schema

```sh
python3 scripts/validate-manifest.py manifest.json
```

The schema is [`manifest.schema.json`](manifest.schema.json).

## Verify macOS signing

```sh
codesign --verify --deep --strict --verbose=2 husk-*-macos-universal2
spctl --assess --type execute --verbose=2 husk-*-macos-universal2
```

A standalone Mach-O executable cannot carry a stapled notarization ticket.
Gatekeeper checks the ticket online when the binary is first run; keep network
access for that first check.

## Build from source instead

```sh
tar -xzf bamhusk-<version>-src.tar.gz
cd bamhusk-<version>
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

Compare your binary's behaviour with `--version` and the smoke test in
`tests/integration/smoke.sh`.

## Security rules

- Never download or execute a binary automatically.
- Compare the checksum before running a downloaded binary.
- Prefer the source tarball when a platform is not listed or a signature does
  not validate.
- Report a mismatch instead of bypassing the check.
