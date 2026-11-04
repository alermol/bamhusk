#!/usr/bin/env bash
set -euo pipefail

husk=${1:?usage: window_metrics.sh /path/to/husk /path/to/input.bam}
input=${2:?usage: window_metrics.sh /path/to/husk /path/to/input.bam}

if ! command -v samtools >/dev/null 2>&1; then
    echo "window-metrics: samtools is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-window-metrics.XXXXXX")
trap 'rm -rf "${work}"' EXIT

fail() {
    echo "window-metrics: $*" >&2
    exit 1
}

cp "${input}" "${work}/before.bam"
samtools index "${work}/before.bam"
"${husk}" "${work}/before.bam" "${work}/after.bam"
samtools index "${work}/after.bam"

window_depth() {
    local bam=$1
    samtools depth -a "${bam}" |
        awk -v width=1000 '{
            window = int(($2 - 1) / width);
            depth[window] += $3;
        } END {
            for (window in depth) print window, depth[window];
        }' | sort -n
}

window_reads() {
    local bam=$1
    samtools view --no-PG "${bam}" |
        awk -F'\t' -v width=1000 '$3 != "*" {
            window = int(($4 - 1) / width);
            reads[window]++;
        } END {
            for (window in reads) print window, reads[window];
        }' | sort -n
}

window_depth "${work}/before.bam" >"${work}/before.depth"
window_depth "${work}/after.bam" >"${work}/after.depth"
if ! diff -u "${work}/before.depth" "${work}/after.depth"; then
    fail "per-window depth changed"
fi

window_reads "${work}/before.bam" >"${work}/before.reads"
window_reads "${work}/after.bam" >"${work}/after.reads"
if ! diff -u "${work}/before.reads" "${work}/after.reads"; then
    fail "per-window read counts changed"
fi

before_count=$(samtools view -c "${work}/before.bam")
after_count=$(samtools view -c "${work}/after.bam")
[[ "${before_count}" -eq "${after_count}" ]] ||
    fail "read count changed: ${before_count} -> ${after_count}"

if [[ -n "${HUSK_METRICS_CMD:-}" ]]; then
    bash -c "${HUSK_METRICS_CMD} \"\$1\"" _ "${work}/before.bam" \
        >"${work}/before.custom"
    bash -c "${HUSK_METRICS_CMD} \"\$1\"" _ "${work}/after.bam" \
        >"${work}/after.custom"
    if ! diff -u "${work}/before.custom" "${work}/after.custom"; then
        fail "downstream metrics command output changed"
    fi
fi

echo "window-metrics: all checks passed"
