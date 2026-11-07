#!/usr/bin/env bash
set -euo pipefail

husk=${1:?usage: smoke.sh /path/to/husk}

if ! command -v samtools >/dev/null 2>&1; then
    echo "smoke: samtools is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-smoke.XXXXXX")
trap 'rm -rf "${work}"' EXIT

cat >"${work}/in.sam" <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:1000
@RG	ID:s1	SM:sample1
@PG	ID:aligner	PN:test
@CO	drop me
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII	RG:Z:s1	NM:i:0
r2	0	chr1	10	60	4M	*	0	0	ACGT	IIII	RG:Z:s1	NM:i:1
SAM

samtools view -b -o "${work}/in.bam" "${work}/in.sam"
"${husk}" "${work}/in.bam" "${work}/out.bam" 2>/dev/null
samtools quickcheck -v "${work}/out.bam"

count=$(samtools view -c "${work}/out.bam")
if [[ "${count}" -ne 2 ]]; then
    echo "smoke: expected 2 reads, got ${count}" >&2
    exit 1
fi

header=$(samtools view --no-PG -H "${work}/out.bam")
grep -q '^@HD' <<<"${header}"
grep -q '^@SQ' <<<"${header}"
grep -q '^@RG' <<<"${header}"
if grep -q '^@PG' <<<"${header}"; then
    echo "smoke: unexpected @PG header line" >&2
    exit 1
fi
if [[ "$(grep -c '^@CO' <<<"${header}")" -ne 1 ]] ||
    ! grep -qE '^@CO[[:space:]]HUSK:1[[:space:]]version=[0-9]+[.][0-9]+[.][0-9]+$' \
        <<<"${header}"; then
    echo "smoke: missing or malformed husk marker" >&2
    exit 1
fi
if grep -q 'drop me' <<<"${header}"; then
    echo "smoke: input @CO line survived" >&2
    exit 1
fi

first=$(samtools view --no-PG "${work}/out.bam" | sed -n '1p')
expected=$'*\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\t*\tRG:Z:s1'
if [[ "${first}" != "${expected}" ]]; then
    printf 'smoke: unexpected record\n  expected: %s\n  actual:   %s\n' \
        "${expected}" "${first}" >&2
    exit 1
fi

"${husk}" --drop-seq "${work}/in.bam" "${work}/no-seq.bam" 2>/dev/null
samtools quickcheck -v "${work}/no-seq.bam"

first=$(samtools view --no-PG "${work}/no-seq.bam" | sed -n '1p')
expected=$'*\t0\tchr1\t1\t60\t4M\t*\t0\t0\t*\t*\tRG:Z:s1'
if [[ "${first}" != "${expected}" ]]; then
    printf 'smoke: unexpected --drop-seq record\n  expected: %s\n  actual:   %s\n' \
        "${expected}" "${first}" >&2
    exit 1
fi
