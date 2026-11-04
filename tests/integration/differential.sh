#!/usr/bin/env bash
set -euo pipefail

husk=${1:?usage: differential.sh /path/to/husk /path/to/fixtures}
fixtures=${2:?usage: differential.sh /path/to/husk /path/to/fixtures}

if ! command -v samtools >/dev/null 2>&1; then
    echo "differential: samtools is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-differential.XXXXXX")
trap 'rm -rf "${work}"' EXIT

fail() {
    echo "differential: $*" >&2
    exit 1
}

view() {
    samtools view --no-PG "$1"
}

checksum() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

compare_fields() {
    local input=$1
    local output=$2
    local first=$3
    local last=$4
    local label=$5
    if ! diff -u \
        <(view "${input}" | cut -f"${first}"-"${last}") \
        <(view "${output}" | cut -f"${first}"-"${last}"); then
        fail "${label}: fields ${first}-${last} differ"
    fi
}

compare_rg() {
    local input=$1
    local output=$2
    if ! awk -F'\t' '
        FNR == NR {
            rg = ""
            for (i = 12; i <= NF; ++i) {
                if ($i ~ /^RG:Z:/) { rg = $i; break }
            }
            expected[FNR] = rg
            next
        }
        {
            rg = ""
            for (i = 12; i <= NF; ++i) {
                if ($i ~ /^RG:Z:/) { rg = $i; break }
            }
            if (rg != expected[FNR]) {
                printf "record %d RG mismatch: %s != %s\n",
                    FNR, expected[FNR], rg > "/dev/stderr"
                exit 1
            }
        }
    ' <(view "${input}") <(view "${output}"); then
        fail "RG tags differ"
    fi
}

check_output_contract() {
    local input=$1
    local output=$2

    samtools quickcheck -v "${output}" || fail "quickcheck failed for ${output}"
    if [[ "$(view "${input}" | wc -l)" -ne "$(view "${output}" | wc -l)" ]]; then
        fail "read count changed for ${output}"
    fi
    if samtools view --no-PG -H "${output}" | grep -qE '^@(PG|CO)'; then
        fail "@PG or @CO survived in ${output}"
    fi
    if view "${output}" | cut -f12- | tr '\t' '\n' |
        grep -vE '^(RG:Z:.*|CG:B:I.*|)$' | grep -q .; then
        fail "unexpected optional tag in ${output}"
    fi
}

# Default transform.
for name in basic multi_sample missing_rg; do
    input="${fixtures}/${name}.bam"
    output="${work}/${name}.out.bam"
    "${husk}" "${input}" "${output}"
    check_output_contract "${input}" "${output}"
    compare_fields "${input}" "${output}" 2 10 "${name} default"
    compare_rg "${input}" "${output}"
done

# Header policy.
header=$(samtools view --no-PG -H "${work}/basic.out.bam")
grep -q '^@HD' <<<"${header}" || fail "missing @HD"
grep -q '^@SQ' <<<"${header}" || fail "missing @SQ"
grep -q '^@RG' <<<"${header}" || fail "missing @RG"
grep -q 'SO:coordinate' <<<"${header}" || fail "missing SO:coordinate"

# Keep read names.
"${husk}" --keep-name "${fixtures}/basic.bam" "${work}/keep-name.bam"
check_output_contract "${fixtures}/basic.bam" "${work}/keep-name.bam"
compare_fields "${fixtures}/basic.bam" "${work}/keep-name.bam" 1 10 "keep-name"

# Keep qualities.
"${husk}" --keep-qual "${fixtures}/basic.bam" "${work}/keep-qual.bam"
check_output_contract "${fixtures}/basic.bam" "${work}/keep-qual.bam"
compare_fields "${fixtures}/basic.bam" "${work}/keep-qual.bam" 2 11 "keep-qual"

# Drop SEQ while keeping CIGAR.
"${husk}" --drop-seq "${fixtures}/basic.bam" "${work}/drop-seq.bam"
check_output_contract "${fixtures}/basic.bam" "${work}/drop-seq.bam"
compare_fields "${fixtures}/basic.bam" "${work}/drop-seq.bam" 2 9 "drop-seq"
if view "${work}/drop-seq.bam" | awk -F'\t' '$10 != "*" { exit 1 }'; then
    :
else
    fail "--drop-seq left SEQ data"
fi

# Drop RG on a single-sample BAM.
"${husk}" --drop-rg "${fixtures}/basic.bam" "${work}/drop-rg.bam"
check_output_contract "${fixtures}/basic.bam" "${work}/drop-rg.bam"
compare_fields "${fixtures}/basic.bam" "${work}/drop-rg.bam" 2 10 "drop-rg"
if view "${work}/drop-rg.bam" | cut -f12- | grep -q 'RG:'; then
    fail "--drop-rg left RG tags"
fi

# Refuse to merge multiple samples.
if "${husk}" --drop-rg "${fixtures}/multi_sample.bam" "${work}/multi-drop.bam" 2>/dev/null; then
    fail "--drop-rg accepted a multi-sample BAM"
fi
[[ ! -e "${work}/multi-drop.bam" ]] || fail "multi-sample refusal left output"

# Index and verification together.
"${husk}" --index --verify "${fixtures}/basic.bam" "${work}/indexed.bam"
[[ -s "${work}/indexed.bam.csi" ]] || fail "missing CSI index"
samtools idxstats "${work}/indexed.bam" >/dev/null || fail "invalid CSI index"
[[ "$(samtools view -c "${work}/indexed.bam" chr1)" -eq 3 ]] ||
    fail "indexed region query returned the wrong count"

# Verify, threads, compression.
"${husk}" --verify --threads 4 "${fixtures}/basic.bam" "${work}/verified.bam"
samtools quickcheck -v "${work}/verified.bam" || fail "verify output invalid"
for level in 0 9; do
    "${husk}" --compression "${level}" "${fixtures}/basic.bam" \
        "${work}/compression-${level}.bam"
    samtools quickcheck -v "${work}/compression-${level}.bam" ||
        fail "compression ${level} output invalid"
done

# Overwrite protection and --force.
"${husk}" "${fixtures}/basic.bam" "${work}/force.bam"
before=$(checksum "${work}/force.bam")
if "${husk}" "${fixtures}/basic.bam" "${work}/force.bam" 2>/dev/null; then
    fail "overwrote an existing output without --force"
fi
after=$(checksum "${work}/force.bam")
[[ "${before}" == "${after}" ]] || fail "output changed without --force"
"${husk}" --force "${fixtures}/basic.bam" "${work}/force.bam"
samtools quickcheck -v "${work}/force.bam" || fail "--force output invalid"

# Empty BAM.
"${husk}" "${fixtures}/empty.bam" "${work}/empty.out.bam"
samtools quickcheck -v "${work}/empty.out.bam" || fail "empty output invalid"
[[ "$(samtools view -c "${work}/empty.out.bam")" -eq 0 ]] ||
    fail "empty BAM gained records"

# Long CIGAR round-trip.
"${husk}" "${fixtures}/long_cigar.bam" "${work}/long-cigar.out.bam"
check_output_contract "${fixtures}/long_cigar.bam" "${work}/long-cigar.out.bam"
input_cigar=$(view "${fixtures}/long_cigar.bam" | cut -f6)
output_cigar=$(view "${work}/long-cigar.out.bam" | cut -f6)
[[ "${input_cigar}" == "${output_cigar}" ]] ||
    fail "long CIGAR changed: ${input_cigar} -> ${output_cigar}"

echo "differential: all checks passed"
