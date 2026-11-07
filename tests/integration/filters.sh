#!/usr/bin/env bash
set -euo pipefail

husk=${1:?usage: filters.sh /path/to/husk /path/to/fixtures}
fixtures=${2:?usage: filters.sh /path/to/husk /path/to/fixtures}

if ! command -v samtools >/dev/null 2>&1; then
    echo "filters: samtools is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-filters.XXXXXX")
trap 'rm -rf "${work}"' EXIT

fail() {
    echo "filters: $*" >&2
    exit 1
}

count() {
    samtools view -c "$1"
}

expect_count() {
    local label=$1
    local expected=$2
    local bam=$3
    local actual
    actual=$(count "${bam}")
    if [[ "${actual}" -ne "${expected}" ]]; then
        fail "${label}: expected ${expected} reads, got ${actual}"
    fi
}

check_marker() {
    local bam=$1
    if ! samtools view --no-PG -H "${bam}" |
        grep -qE '^@CO[[:space:]]HUSK:1[[:space:]]version='; then
        fail "missing husk marker in ${bam}"
    fi
}

"${husk}" --exclude-flags UNMAP "${fixtures}/basic.bam" \
    "${work}/exclude-flags.bam"
samtools quickcheck -v "${work}/exclude-flags.bam"
expect_count "exclude flags" \
    "$(samtools view -c -F UNMAP "${fixtures}/basic.bam")" \
    "${work}/exclude-flags.bam"
check_marker "${work}/exclude-flags.bam"

"${husk}" --require-flags REVERSE "${fixtures}/basic.bam" \
    "${work}/require-flags.bam"
samtools quickcheck -v "${work}/require-flags.bam"
expect_count "require flags" \
    "$(samtools view -c -f REVERSE "${fixtures}/basic.bam")" \
    "${work}/require-flags.bam"

"${husk}" --min-mapq 40 "${fixtures}/basic.bam" \
    "${work}/min-mapq.bam"
samtools quickcheck -v "${work}/min-mapq.bam"
expect_count "minimum MAPQ" \
    "$(samtools view -c -q 40 "${fixtures}/basic.bam")" \
    "${work}/min-mapq.bam"

"${husk}" --min-read-length 1 "${fixtures}/basic.bam" \
    "${work}/min-length.bam"
samtools quickcheck -v "${work}/min-length.bam"
expect_count "minimum read length" \
    "$(samtools view -c -m 1 "${fixtures}/basic.bam")" \
    "${work}/min-length.bam"

"${husk}" --exclude-flags UNMAP --min-mapq 30 "${fixtures}/basic.bam" \
    "${work}/combined.bam"
samtools quickcheck -v "${work}/combined.bam"
expect_count "combined filters" \
    "$(samtools view -c -F UNMAP -q 30 "${fixtures}/basic.bam")" \
    "${work}/combined.bam"

"${husk}" --filter-expr 'mapq >= 40' "${fixtures}/basic.bam" \
    "${work}/expr-mapq.bam"
samtools quickcheck -v "${work}/expr-mapq.bam"
expect_count "MAPQ expression" \
    "$(samtools view -c -e 'mapq >= 40' "${fixtures}/basic.bam")" \
    "${work}/expr-mapq.bam"

"${husk}" --filter-expr '[NM] == 0' "${fixtures}/basic.bam" \
    "${work}/expr-tag.bam"
samtools quickcheck -v "${work}/expr-tag.bam"
expect_count "tag expression" \
    "$(samtools view -c -e '[NM] == 0' "${fixtures}/basic.bam")" \
    "${work}/expr-tag.bam"

# Filters are applied before downsampling.
"${husk}" --exclude-flags UNMAP --min-mapq 20 --downsample-count 2 \
    --downsample-seed 7 --keep-name "${fixtures}/basic.bam" \
    "${work}/filter-downsample.bam"
samtools quickcheck -v "${work}/filter-downsample.bam"
expect_count "filtered downsampling" 2 "${work}/filter-downsample.bam"
allowed=$(
    samtools view --no-PG -F UNMAP -q 20 "${fixtures}/basic.bam" |
        cut -f1 | sort -u
)
while IFS= read -r name; do
    if ! grep -qxF "${name}" <<<"${allowed}"; then
        fail "filtered downsampling kept a filtered-out read: ${name}"
    fi
done < <(samtools view --no-PG "${work}/filter-downsample.bam" | cut -f1)

# Filtering composes with indexing and verification.
"${husk}" --filter-expr 'mapq >= 30' --index --verify \
    "${fixtures}/basic.bam" "${work}/filter-indexed.bam"
samtools quickcheck -v "${work}/filter-indexed.bam"
samtools idxstats "${work}/filter-indexed.bam" >/dev/null ||
    fail "invalid CSI index"

# A filter can remove every record and still produce a valid BAM.
"${husk}" --require-flags UNMAP --filter-expr 'mapq > 100' \
    "${fixtures}/basic.bam" "${work}/empty-filter.bam"
samtools quickcheck -v "${work}/empty-filter.bam"
expect_count "empty filter" 0 "${work}/empty-filter.bam"

# Invalid expressions fail without leaving an output.
if "${husk}" --filter-expr '(' "${fixtures}/basic.bam" \
    "${work}/invalid-expr.bam" 2>/dev/null; then
    fail "invalid filter expression was accepted"
fi
[[ ! -e "${work}/invalid-expr.bam" ]] ||
    fail "invalid filter expression left an output"

echo "filters: all checks passed"
