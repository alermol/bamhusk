# Husk specification

**Status:** v0.1.0 contract.

## 1. Purpose

Husk is a standalone, cross-platform command-line tool that reduces a
coordinate-sorted BAM to the minimum representation a downstream analysis
application needs. A consuming application recommends Husk when an upload
exceeds its server-side size limit. Husk runs on the user's machine; the
application never receives or executes the binary.

## 2. Scope

### Goals

- Preserve every alignment record and every field the downstream analysis
  reads.
- Remove data that the downstream analysis does not read.
- Produce output that passes `samtools quickcheck`.
- Ship reproducible binaries for Linux, macOS, and Windows.
- Give users a verifiable, signed release artifact and a source fallback.

### Non-goals

- Read filtering, trimming, duplicate removal, or realignment.
- CRAM support in v0.1.
- Server-side execution of user-uploaded binaries.
- Preserving base qualities or read names by default.
- Replacing `samtools` as a general BAM toolkit.

## 3. Definitions

**Slimming** rewrites a BAM with the same records in the same order, but with
only the header lines and record fields listed in sections 6 and 7.

**Kept fields** are the BAM core fields `FLAG`, `RNAME`, `POS`, `MAPQ`,
`CIGAR`, `RNEXT`, `PNEXT`, and `TLEN`, plus the optional `RG:Z` tag. Their
values are copied unchanged.

## 4. CLI contract

```text
husk [options] <in.bam> <out.bam>

Options:
  --drop-seq          Remove SEQ (read length comes from CIGAR)
  --keep-qual         Keep QUAL (requires SEQ)
  --keep-name         Keep read names
  --drop-rg           Drop RG tags (single-sample only)
  -@, --threads N     htslib I/O threads (default: min(4, cores))
  --index             Write out.bam.csi with min_shift=14
  --compression N     BGZF compression level 0-9 (default 6)
  --force             Overwrite an existing output
  --verify            Reopen and validate the output after writing
  --version           Print version and git SHA
  --help              Print usage
```

`--` ends option parsing. `-h` is an alias for `--help`. Value options accept
both `--option value` and `--option=value`.

### Exit codes

| Code | Meaning |
|---:|---|
| 0 | Success |
| 1 | Runtime, I/O, or validation error |
| 2 | Usage error |
| 3 | Verification failure |

On success, Husk writes a one-line read and byte summary to stderr. Standard
output is used only by `--help` and `--version`.

### Defaults

- Drop `QUAL`, read names (written as `*`), and all optional tags except `RG`.
- Keep `SEQ`.
- Refuse to overwrite an existing output unless `--force` is given.
- Refuse an output path that resolves to the input path.
- Write to `<out>.tmp.<pid>` in the output directory (or a numbered variant
  if that name is occupied) and rename atomically after a successful close.
- Remove the temporary file on error or `SIGINT`.
- `--keep-qual` with `--drop-seq` is a usage error.
- `--threads` must be at least 1; `--compression` must be in 0-9.
- `--drop-rg` is only valid when the header can be shown to describe a single
  sample. Husk refuses when the header declares more than one distinct `SM`
  value; a read group without a non-empty `SM` counts as an unknown sample.
- An input path that is also the output `.csi` path is a usage error.

A failed run leaves any pre-existing BAM and adjacent index untouched. The
BAM and index are moved into place as one finalization step; if a replacement
fails, the previous index is restored. Replacing a BAM without `--index`
removes a stale adjacent `.csi`.

## 5. Input requirements

- BAM only. CRAM and SAM input are rejected in v0.1.
- The input must be coordinate-sorted. Husk does not re-sort.
- The input header must contain at least one `@SQ` line. A BAM without
  reference targets is rejected because its output would fail
  `samtools quickcheck`.
- An empty BAM with at least one `@SQ` line is valid input and produces an
  empty BAM.

## 6. Header policy

Keep, in their original order:

- `@HD`
- `@SQ`
- `@RG`

Drop every other header line, including `@PG`, `@CO`, and unknown record
types. No synthetic lines are added. Kept lines are copied byte-for-byte, so
`@HD SO:coordinate` and all `@SQ`/`@RG` fields survive.

If no `@SQ` line is present, exit 1.

## 7. Record policy

Every input record produces exactly one output record, in input order.

| Field | Default | `--keep-name` | `--keep-qual` | `--drop-seq` | `--drop-rg` |
|---|---|---|---|---|---|
| `QNAME` | `*` | kept | `*` | `*` | `*` |
| `FLAG` | kept | kept | kept | kept | kept |
| `RNAME` | kept | kept | kept | kept | kept |
| `POS` | kept | kept | kept | kept | kept |
| `MAPQ` | kept | kept | kept | kept | kept |
| `CIGAR` | kept | kept | kept | kept | kept |
| `RNEXT` | kept | kept | kept | kept | kept |
| `PNEXT` | kept | kept | kept | kept | kept |
| `TLEN` | kept | kept | kept | kept | kept |
| `SEQ` | kept | kept | kept | dropped | kept |
| `QUAL` | dropped | dropped | kept | dropped | dropped |
| Optional tags | `RG` only | `RG` only | `RG` only | `RG` only | none |

`--keep-qual` preserves the original quality string only when it is present;
missing qualities stay missing. Dropping `SEQ` must not change the inferred
read length, which remains the sum of `CIGAR` query-consuming operations.

A CIGAR longer than 65535 operations is stored by htslib as a two-operation
placeholder plus a `CG:B:I` tag. Husk preserves that tag when required so the
real CIGAR survives.

## 8. Validation

A successful run guarantees:

- `samtools quickcheck` passes.
- The read count matches the input.
- Every kept core field matches the input record-by-record.
- `RG:Z` matches the input when retained.
- The header contains only `@HD`, `@SQ`, and `@RG`.
- The output is a valid BAM with a readable header and decodable records.

With `--verify`, Husk reopens the output, decodes the header and every record,
rechecks the read count, and validates the index when `--index` was used. A
verification failure exits 3.

## 9. Release contract

A release is identified by a tag `v<version>` and contains:

| Asset | Purpose |
|---|---|
| `husk-<version>-linux-x86_64` | Static Linux binary |
| `husk-<version>-linux-aarch64` | Static Linux binary |
| `husk-<version>-macos-universal2` | Signed macOS binary; notarised when Apple credentials are configured |
| `husk-<version>-windows-x86_64.exe` | Windows binary |
| `bamhusk-<version>-src.tar.gz` | Source tarball |
| `SHA256SUMS` | Checksums for the binaries and source tarball |
| `manifest.json` | Machine-readable asset index |

`manifest.json` uses schema 1:

```json
{
  "schema": 1,
  "tool": "husk",
  "version": "0.1.0",
  "source": "https://github.com/alermol/bamhusk/releases/download/v0.1.0/bamhusk-0.1.0-src.tar.gz",
  "assets": [
    {
      "os": "linux",
      "arch": "x86_64",
      "url": "https://github.com/alermol/bamhusk/releases/download/v0.1.0/husk-0.1.0-linux-x86_64",
      "sha256": "...",
      "size": 1234567
    }
  ]
}
```

Every binary asset appears once in `assets` with `os`, `arch`, `url`,
`sha256`, and `size`; the source tarball is named by `source` and has a
checksum in `SHA256SUMS`. The manifest validates against the published JSON
schema. Checksums in `manifest.json` and `SHA256SUMS` agree. Binaries are
release assets only and are never committed to git.

The Docker workflow additionally publishes a multi-arch image to
`ghcr.io/alermol/bamhusk`. The image is not part of the signed release asset
set.

## 10. Consumer integration requirements

The consuming application:

- Detects oversized files before upload and never sends them to the server.
- Shows the file size, the limit, the detected platform, a release link, a
  source link, and the exact `husk` command.
- Offers a manual platform selector and defaults macOS to universal2.
- Displays SHA-256 checksums and verification instructions.
- Falls back to the GitHub release page when the manifest is unavailable.
- Never downloads or executes a binary automatically.

## 11. Acceptance

v0.1.0 is accepted when:

- All validation rules in section 8 hold on synthetic and real BAMs.
- Downstream window-metric counts are identical before and after slimming.
- The build matrix produces every asset in section 9.
- Every binary asset has a matching checksum and manifest entry; the source
  tarball has a matching checksum in `SHA256SUMS`.
- A new user can build from source, verify a release, slim a BAM, and upload
  it without reading the implementation.

## 12. License

MIT. See `LICENSE`.
