# Releasing Husk

## Versioning

The version is defined once in `CMakeLists.txt` (`project(... VERSION ...)`).
Release tags use `v<version>`, for example `v0.1.0`.

## Release flow

1. Confirm `main` is green in CI.
2. Update `CHANGELOG.md`.
3. Commit the release state.
4. Tag and push:

   ```sh
   git tag v0.1.0
   git push origin v0.1.0
   ```

5. `.github/workflows/release.yml` builds every platform, creates the source
   tarball, generates `manifest.json` and `SHA256SUMS`, and publishes the
   GitHub release.

The workflow can also be started manually with a version input.

## Assets

| Asset | Built by |
|---|---|
| `husk-<version>-linux-x86_64` | `scripts/build-static.sh` |
| `husk-<version>-linux-aarch64` | `scripts/build-static.sh` |
| `husk-<version>-macos-universal2` | `scripts/build-macos-universal.sh` |
| `husk-<version>-windows-x86_64.exe` | `scripts/build-windows.sh` |
| `bamhusk-<version>-src.tar.gz` | `git archive` |
| `SHA256SUMS` | `scripts/make-manifest.py` |
| `manifest.json` | `scripts/make-manifest.py` |

`manifest.json` follows schema 1 in
[`manifest.schema.json`](manifest.schema.json). The publish job fails if a
platform asset is missing or the manifest is invalid.

## Signing and notarisation

The macOS job uses these repository secrets when present:

| Secret | Purpose |
|---|---|
| `MACOS_CERTIFICATE_P12` | Base64-encoded Developer ID certificate |
| `MACOS_CERTIFICATE_PASSWORD` | Certificate password |
| `MACOS_SIGN_IDENTITY` | Codesign identity, for example `Developer ID Application: ...` |
| `APPLE_ID` | Apple ID for notarisation |
| `APPLE_TEAM_ID` | Apple team ID |
| `APPLE_APP_PASSWORD` | App-specific password |

Without them, the macOS build is ad-hoc signed and is not notarised. The
manifest and checksums still protect integrity. Configure every secret above
before publishing a release that must satisfy the notarised-asset contract.

## Docker

The `Dockerfile` produces a scratch image containing the fully static Linux
binary. `.github/workflows/docker.yml` builds and pushes a multi-arch image to
GHCR on tags.

## Release checklist

- [ ] Version in `CMakeLists.txt` matches the tag.
- [ ] `CHANGELOG.md` has an entry for the version.
- [ ] All four platform assets are present.
- [ ] `manifest.json` validates against the schema.
- [ ] `SHA256SUMS` verifies.
- [ ] macOS signature validates; notarisation validates when configured.
- [ ] Each binary passes `tests/integration/smoke.sh`.
- [ ] The source tarball builds and tests.
