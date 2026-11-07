#!/usr/bin/env bash
set -euo pipefail

husk=${1:?usage: edge_cases.sh /path/to/husk /path/to/fixtures}
fixtures=${2:?usage: edge_cases.sh /path/to/husk /path/to/fixtures}

if ! command -v samtools >/dev/null 2>&1; then
    echo "edge-cases: samtools is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-edge-cases.XXXXXX")
trap 'rm -rf "${work}"' EXIT

fail() {
    echo "edge-cases: $*" >&2
    exit 1
}

checksum() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

no_temp_files() {
    [[ -z "$(find "${work}" -maxdepth 1 -name '*.tmp.*' -print -quit)" ]]
}

# Empty BAM with a valid header.
"${husk}" "${fixtures}/empty.bam" "${work}/empty.bam"
samtools quickcheck -v "${work}/empty.bam"
[[ "$(samtools view -c "${work}/empty.bam")" -eq 0 ]] ||
    fail "empty BAM gained records"
grep -qE '^@CO[[:space:]]HUSK:1[[:space:]]version=' \
    <<<"$(samtools view --no-PG -H "${work}/empty.bam")" ||
    fail "empty BAM lost the husk marker"

# Missing @HD is allowed; surviving lines are copied.
"${husk}" "${fixtures}/no_hd.bam" "${work}/no-hd.bam"
samtools quickcheck -v "${work}/no-hd.bam"
header=$(samtools view --no-PG -H "${work}/no-hd.bam")
grep -q '^@SQ' <<<"${header}" || fail "no-hd lost @SQ"
grep -qE '^@CO[[:space:]]HUSK:1[[:space:]]version=' <<<"${header}" ||
    fail "no-hd lost the husk marker"
if grep -q '^@HD' <<<"${header}"; then
    fail "no-hd gained a synthetic @HD"
fi

# Missing RG is valid; the record simply has no RG tag.
"${husk}" "${fixtures}/missing_rg.bam" "${work}/missing-rg.bam"
samtools quickcheck -v "${work}/missing-rg.bam"
if samtools view --no-PG "${work}/missing-rg.bam" | cut -f12- |
    grep 'RG:' >/dev/null; then
    fail "missing-rg output gained an RG tag"
fi

# Malformed RG is rejected without leaving output or temp files.
if "${husk}" "${fixtures}/malformed_rg.bam" "${work}/malformed.bam" 2>/dev/null; then
    fail "malformed RG was accepted"
fi
[[ ! -e "${work}/malformed.bam" ]] || fail "malformed RG left output"
no_temp_files || fail "malformed RG left a temporary file"

# A failed forced run must not destroy the previous output.
printf 'previous output\n' >"${work}/protected.bam"
before=$(checksum "${work}/protected.bam")
if "${husk}" --force "${fixtures}/malformed_rg.bam" \
    "${work}/protected.bam" 2>/dev/null; then
    fail "malformed RG was accepted with --force"
fi
after=$(checksum "${work}/protected.bam")
[[ "${before}" == "${after}" ]] || fail "failed run overwrote the old output"

# Unsorted input is rejected.
if "${husk}" "${fixtures}/unsorted.bam" "${work}/unsorted.bam" 2>/dev/null; then
    fail "unsorted BAM was accepted"
fi
[[ ! -e "${work}/unsorted.bam" ]] || fail "unsorted BAM left output"

# A header with no @HD/@SQ/@RG is rejected.
if "${husk}" "${fixtures}/no_usable_header.bam" \
    "${work}/no-usable.bam" 2>/dev/null; then
    fail "header with no usable lines was accepted"
fi
[[ ! -e "${work}/no-usable.bam" ]] || fail "unusable header left output"

# A header without @SQ cannot satisfy samtools quickcheck.
if "${husk}" "${fixtures}/no_sq.bam" "${work}/no-sq.bam" 2>/dev/null; then
    fail "header without @SQ was accepted"
fi
[[ ! -e "${work}/no-sq.bam" ]] || fail "header without @SQ left output"

# A read group without SM is an unknown sample and cannot be merged.
if "${husk}" --drop-rg "${fixtures}/mixed_sample.bam" \
    "${work}/mixed-drop.bam" 2>/dev/null; then
    fail "--drop-rg merged an unknown sample"
fi
[[ ! -e "${work}/mixed-drop.bam" ]] || fail "mixed-sample refusal left output"

# Input and output must be different files.
before=$(checksum "${fixtures}/basic.bam")
if "${husk}" "${fixtures}/basic.bam" "${fixtures}/basic.bam" 2>/dev/null; then
    fail "input == output was accepted"
fi
after=$(checksum "${fixtures}/basic.bam")
[[ "${before}" == "${after}" ]] || fail "input == output changed the input"

# An input BAM that is also the output index path must not be removed.
cp "${fixtures}/basic.bam" "${work}/collision.bam.csi"
before=$(checksum "${work}/collision.bam.csi")
if "${husk}" "${work}/collision.bam.csi" "${work}/collision.bam" 2>/dev/null; then
    fail "input at the output index path was accepted"
fi
after=$(checksum "${work}/collision.bam.csi")
[[ "${before}" == "${after}" ]] || fail "input at the output index path was changed"

# An unusable index path must not replace the existing BAM.
printf 'previous output\n' >"${work}/index-directory.bam"
mkdir "${work}/index-directory.bam.csi"
before=$(checksum "${work}/index-directory.bam")
if "${husk}" --force --index "${fixtures}/basic.bam" \
    "${work}/index-directory.bam" 2>/dev/null; then
    fail "directory index path was accepted"
fi
after=$(checksum "${work}/index-directory.bam")
[[ "${before}" == "${after}" ]] || fail "failed index run replaced the old BAM"

# SAM input is rejected.
cat >"${work}/input.sam" <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:1000
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII
SAM
if "${husk}" "${work}/input.sam" "${work}/sam-out.bam" 2>/dev/null; then
    fail "SAM input was accepted"
fi

# Usage errors.
if "${husk}" --drop-seq --keep-qual "${fixtures}/basic.bam" \
    "${work}/usage.bam" 2>/dev/null; then
    fail "--drop-seq --keep-qual was accepted"
fi
if "${husk}" --threads 0 "${fixtures}/basic.bam" "${work}/usage.bam" 2>/dev/null; then
    fail "--threads 0 was accepted"
fi
if "${husk}" --compression 10 "${fixtures}/basic.bam" \
    "${work}/usage.bam" 2>/dev/null; then
    fail "--compression 10 was accepted"
fi
if "${husk}" "${fixtures}/basic.bam" 2>/dev/null; then
    fail "missing output was accepted"
fi

# Output path must be usable.
if "${husk}" "${fixtures}/basic.bam" \
    "${work}/missing-directory/out.bam" 2>/dev/null; then
    fail "missing output directory was accepted"
fi
mkdir "${work}/directory.bam"
if "${husk}" --force "${fixtures}/basic.bam" \
    "${work}/directory.bam" 2>/dev/null; then
    fail "directory output was accepted"
fi

# Version and help.
"${husk}" --version | grep '^husk [0-9]' >/dev/null
"${husk}" --help | grep '^Usage: husk' >/dev/null

no_temp_files || fail "a failure left a temporary file"

echo "edge-cases: all checks passed"
