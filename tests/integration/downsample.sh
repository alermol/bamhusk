#!/usr/bin/env bash
set -euo pipefail

husk=${1:?usage: downsample.sh /path/to/husk /path/to/fixtures}
fixtures=${2:?usage: downsample.sh /path/to/husk /path/to/fixtures}

if ! command -v samtools >/dev/null 2>&1; then
    echo "downsample: samtools is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-downsample.XXXXXX")
trap 'rm -rf "${work}"' EXIT

fail() {
    echo "downsample: $*" >&2
    exit 1
}

count() {
    samtools view -c "$1"
}

rg_count() {
    samtools view --no-PG "$1" |
        awk -F'\t' -v read_group="$2" '{
            for (i = 12; i <= NF; ++i) {
                if ($i == "RG:Z:" read_group) {
                    ++count
                }
            }
        } END { print count + 0 }'
}

# Total mode is the default.
"${husk}" --downsample-count 2 "${fixtures}/basic.bam" \
    "${work}/total-count.bam"
samtools quickcheck -v "${work}/total-count.bam"
[[ "$(count "${work}/total-count.bam")" -eq 2 ]] ||
    fail "total count did not keep 2 reads"

"${husk}" --downsample-fraction 0.5 "${fixtures}/basic.bam" \
    "${work}/total-fraction.bam"
samtools quickcheck -v "${work}/total-fraction.bam"
[[ "$(count "${work}/total-fraction.bam")" -eq 2 ]] ||
    fail "total fraction did not keep 2 of 4 reads"

# Sample mode applies the target to each sample. multi_sample has two alpha
# reads and one beta read, so a target of one produces one of each.
"${husk}" --downsample-count 1 --downsample-mode sample \
    "${fixtures}/multi_sample.bam" "${work}/sample-count.bam"
samtools quickcheck -v "${work}/sample-count.bam"
[[ "$(count "${work}/sample-count.bam")" -eq 2 ]] ||
    fail "sample count did not keep one read per sample"
[[ "$(rg_count "${work}/sample-count.bam" a)" -eq 1 ]] ||
    fail "sample count did not keep one alpha read"
[[ "$(rg_count "${work}/sample-count.bam" b)" -eq 1 ]] ||
    fail "sample count did not keep one beta read"

# Read groups without SM and reads without RG share one unknown group. The
# mixed_sample fixture therefore has two groups and keeps one read from each.
"${husk}" --downsample-count 1 --downsample-mode sample \
    "${fixtures}/mixed_sample.bam" "${work}/mixed-sample.bam"
[[ "$(count "${work}/mixed-sample.bam")" -eq 2 ]] ||
    fail "sample count did not keep one alpha and one unknown read"

# A fraction of one and a count above the input size keep everything.
"${husk}" --downsample-fraction 1 --downsample-mode sample \
    "${fixtures}/multi_sample.bam" "${work}/all-fraction.bam"
[[ "$(count "${work}/all-fraction.bam")" -eq 3 ]] ||
    fail "fraction of one lost reads"

"${husk}" --downsample-count 100 "${fixtures}/multi_sample.bam" \
    "${work}/all-count.bam"
[[ "$(count "${work}/all-count.bam")" -eq 3 ]] ||
    fail "oversized count lost reads"

# Reads without a known sample still form a group.
"${husk}" --downsample-count 1 --downsample-mode sample \
    "${fixtures}/missing_rg.bam" "${work}/missing-rg.bam"
[[ "$(count "${work}/missing-rg.bam")" -eq 1 ]] ||
    fail "sample mode dropped an ungrouped read"

# The seed makes selection reproducible.
"${husk}" --downsample-count 2 --downsample-seed 7 \
    "${fixtures}/basic.bam" "${work}/seed-1.bam"
"${husk}" --downsample-count 2 --downsample-seed 7 \
    "${fixtures}/basic.bam" "${work}/seed-2.bam"
if ! diff -u \
    <(samtools view --no-PG "${work}/seed-1.bam") \
    <(samtools view --no-PG "${work}/seed-2.bam"); then
    fail "the same seed produced different reads"
fi

# Downsampling composes with indexing and verification.
"${husk}" --downsample-count 2 --index --verify "${fixtures}/basic.bam" \
    "${work}/indexed.bam"
[[ -s "${work}/indexed.bam.csi" ]] || fail "missing CSI index"
samtools idxstats "${work}/indexed.bam" >/dev/null || fail "invalid CSI index"
[[ "$(count "${work}/indexed.bam")" -eq 2 ]] ||
    fail "indexed output has the wrong read count"

# Usage errors.
if "${husk}" --downsample-fraction 0.5 --downsample-count 10 \
    "${fixtures}/basic.bam" "${work}/both.bam" 2>/dev/null; then
    fail "fraction and count were both accepted"
fi
if "${husk}" --downsample-mode sample "${fixtures}/basic.bam" \
    "${work}/mode-only.bam" 2>/dev/null; then
    fail "mode without an amount was accepted"
fi
if "${husk}" --downsample-seed 1 "${fixtures}/basic.bam" \
    "${work}/seed-only.bam" 2>/dev/null; then
    fail "seed without an amount was accepted"
fi
if "${husk}" --downsample-fraction 0 "${fixtures}/basic.bam" \
    "${work}/zero-fraction.bam" 2>/dev/null; then
    fail "fraction zero was accepted"
fi
if "${husk}" --downsample-count 0 "${fixtures}/basic.bam" \
    "${work}/zero-count.bam" 2>/dev/null; then
    fail "count zero was accepted"
fi
if "${husk}" --downsample-mode bogus --downsample-count 1 \
    "${fixtures}/basic.bam" "${work}/bad-mode.bam" 2>/dev/null; then
    fail "an unknown mode was accepted"
fi
[[ ! -e "${work}/both.bam" && ! -e "${work}/mode-only.bam" &&
    ! -e "${work}/seed-only.bam" && ! -e "${work}/zero-fraction.bam" &&
    ! -e "${work}/zero-count.bam" && ! -e "${work}/bad-mode.bam" ]] ||
    fail "a usage error left an output file"

echo "downsample: all checks passed"
