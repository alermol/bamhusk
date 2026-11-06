# Husk

**Keep the grain. Drop the husk.**

Husk rewrites a coordinate-sorted BAM so it contains only the header lines and
record fields downstream analysis needs. It keeps alignments intact while
dropping base qualities, read names, and optional tags by default.

```sh
husk input.bam output.bam
```

A consuming application can recommend Husk when an upload exceeds its
server-side size limit. The application never receives or executes the binary.

## Features

- Keeps `@HD`, `@SQ`, `@RG`, and the alignment fields `FLAG`, `RNAME`, `POS`,
  `MAPQ`, `CIGAR`, `RNEXT`, `PNEXT`, `TLEN`, and `SEQ`.
- Keeps `RG:Z` for multi-sample BAMs; refuses `--drop-rg` when the header
  declares multiple samples.
- Preserves long CIGARs through the `CG:B:I` tag.
- Writes atomically to a temporary file, refuses accidental overwrites, and
  cleans up on failure or `SIGINT`.
- Optional CSI index, output verification, BGZF compression control, and
  htslib I/O threads.
- Optional reproducible random downsampling by fraction or read count, either
  across the whole file or per sample.
- Rejects SAM/CRAM input, malformed `RG` tags, and unsorted records.

## Build

Requirements: C++23 compiler (GCC 13+ or Clang 18+), CMake 3.28+, Ninja,
htslib 1.21+ (release binaries use 1.25), and samtools for the
integration tests.

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

The binary is `build/husk`. See [`docs/build.md`](docs/build.md) for static
Linux, macOS universal2, Windows, sanitizer, and fuzzer builds.

## Usage

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
  --downsample-fraction F
                      Keep a random fraction F of reads (0 < F <= 1)
  --downsample-count N
                      Keep N random reads per downsampling group
  --downsample-mode MODE
                      sample or total (default: total)
  --downsample-seed N
                      Random seed (default: 0)
  --force             Overwrite an existing output
  --verify            Reopen and validate the output after writing
  --version           Print version and git SHA
  --help              Print usage
```

Exit codes: `0` success, `1` runtime/I/O/validation error, `2` usage error,
`3` verification failure.

`--downsample-mode sample` groups records by the `SM` value of their `RG:Z`
read group. Multiple read groups with the same `SM` form one sample; reads
without a known sample share one unknown group. Sampling is record-level and
does not keep mates together. The target is applied exactly per group, and
`--downsample-seed` makes the selection reproducible. Downsampling reads the
input twice: once to count each group and once to write the selected records.

The full contract, including the release manifest, is in
[`docs/spec.md`](docs/spec.md).

## Verification

Every binary release asset has a SHA-256 checksum in `SHA256SUMS` and
`manifest.json`; the source tarball has a checksum in `SHA256SUMS`. See
[`docs/verification.md`](docs/verification.md) for platform-specific commands.
Never download or execute a binary automatically.

## Documentation

- [`docs/spec.md`](docs/spec.md)
- [`docs/build.md`](docs/build.md)
- [`docs/verification.md`](docs/verification.md)

## License

MIT. See [`LICENSE`](LICENSE).
