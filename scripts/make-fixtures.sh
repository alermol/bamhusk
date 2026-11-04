#!/usr/bin/env bash
set -euo pipefail

destination=${1:-"${TMPDIR:-/tmp}/husk-fixtures"}

if ! command -v samtools >/dev/null 2>&1; then
    echo "make-fixtures: samtools is required" >&2
    exit 77
fi

rm -rf "${destination}"
mkdir -p "${destination}"

sam_to_bam() {
    local name=$1
    samtools view --no-PG -b -o "${destination}/${name}.bam" -
}

sam_to_bam basic <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:100000
@SQ	SN:chr2	LN:100000
@RG	ID:s1	SM:sample1
@PG	ID:aligner	PN:test
@CO	drop me
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII	RG:Z:s1	NM:i:0	MD:Z:4
r2	16	chr1	10	30	2M2M	*	0	0	ACGT	IIII	RG:Z:s1	NM:i:1
r4	0	chr1	20	60	1M1I2M	*	0	0	ACGT	IIII	RG:Z:s1	AS:i:10
r3	4	*	0	0	*	*	0	0	ACGT	IIII
SAM

sam_to_bam multi_sample <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:100000
@RG	ID:a	SM:alpha
@RG	ID:b	SM:beta
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII	RG:Z:a
r2	0	chr1	2	60	4M	*	0	0	ACGT	IIII	RG:Z:b
r3	0	chr1	3	60	4M	*	0	0	ACGT	IIII	RG:Z:a
SAM

sam_to_bam empty <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:100000
@RG	ID:s1	SM:sample1
SAM

sam_to_bam no_hd <<'SAM'
@SQ	SN:chr1	LN:100000
@RG	ID:s1	SM:sample1
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII	RG:Z:s1
SAM

sam_to_bam missing_rg <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:100000
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII
SAM

sam_to_bam malformed_rg <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:100000
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII	RG:i:1
SAM

sam_to_bam unsorted <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:100000
r1	0	chr1	20	60	4M	*	0	0	ACGT	IIII
r2	0	chr1	10	60	4M	*	0	0	ACGT	IIII
SAM

sam_to_bam no_usable_header <<'SAM'
@PG	ID:aligner	PN:test
SAM

sam_to_bam no_sq <<'SAM'
@HD	VN:1.6	SO:coordinate
@RG	ID:s1	SM:sample1
SAM

sam_to_bam mixed_sample <<'SAM'
@HD	VN:1.6	SO:coordinate
@SQ	SN:chr1	LN:100000
@RG	ID:a	SM:alpha
@RG	ID:b
r1	0	chr1	1	60	4M	*	0	0	ACGT	IIII	RG:Z:a
r2	0	chr1	2	60	4M	*	0	0	ACGT	IIII	RG:Z:b
SAM

awk 'BEGIN {
    printf "@HD\tVN:1.6\tSO:coordinate\n";
    printf "@SQ\tSN:chr1\tLN:100000\n";
    printf "@RG\tID:s1\tSM:sample1\n";
    printf "long\t0\tchr1\t1\t60\t";
    for (i = 0; i < 70000; ++i) printf "1M";
    printf "\t*\t0\t0\t";
    for (i = 0; i < 70000; ++i) printf "A";
    printf "\t";
    for (i = 0; i < 70000; ++i) printf "I";
    printf "\tRG:Z:s1\n";
}' | sam_to_bam long_cigar

printf '%s\n' "${destination}"
