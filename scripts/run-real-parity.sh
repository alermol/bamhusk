#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: run-real-parity.sh <input.bam>" >&2
    echo "env: HUSK (default build/husk), HUSK_METRICS_CMD" >&2
    exit 2
fi

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
husk=${HUSK:-"${root}/build/husk"}
input=$1

if [[ ! -x "${husk}" ]]; then
    echo "run-real-parity: husk binary not found: ${husk}" >&2
    exit 1
fi
if [[ ! -f "${input}" ]]; then
    echo "run-real-parity: input BAM not found: ${input}" >&2
    exit 1
fi

"${root}/tests/integration/window_metrics.sh" "${husk}" "${input}"
