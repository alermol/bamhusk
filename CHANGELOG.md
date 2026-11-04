# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]

### Fixed

- Reject input headers without `@SQ`, which could not satisfy
  `samtools quickcheck`.
- Reject malformed `RG` auxiliary data instead of silently dropping the tag.
- Treat a read group without `SM` as an unknown sample for `--drop-rg`.
- Refuse an input path that is also the output `.csi` path.
- Preserve the previous BAM and index when index finalization fails.
- Reserve both temporary BAM and CSI names before writing.
- Include the commit SHA in source-tarball builds.
- Do not attempt to staple a standalone macOS Mach-O; Gatekeeper checks the
  notarization ticket online.

### Changed

- Rebuild records by copying and trimming the original BAM data instead of
  decoding and re-encoding `SEQ` for every read.
- Remove per-asset `.sha256` sidecars from the release output.
- Document the GHCR image and conditional macOS notarisation in the spec.

## [0.1.0] - 2026-11-04

First public release.
