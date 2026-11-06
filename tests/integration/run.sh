#!/usr/bin/env bash
set -euo pipefail

husk=${1:?usage: run.sh /path/to/husk}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

if ! command -v samtools >/dev/null 2>&1; then
    echo "integration: samtools is not installed; skipping" >&2
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/husk-integration.XXXXXX")
trap 'rm -rf "${work}"' EXIT

"${root}/scripts/make-fixtures.sh" "${work}/fixtures" >/dev/null
"${root}/tests/integration/smoke.sh" "${husk}"
"${root}/tests/integration/differential.sh" "${husk}" "${work}/fixtures"
"${root}/tests/integration/edge_cases.sh" "${husk}" "${work}/fixtures"
"${root}/tests/integration/downsample.sh" "${husk}" "${work}/fixtures"
"${root}/tests/integration/window_metrics.sh" "${husk}" "${work}/fixtures/basic.bam"

echo "integration: all checks passed"
