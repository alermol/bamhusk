#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    cat >&2 <<'USAGE'
usage: fetch-real-data.sh <bam-url> [destination]

Downloads a public BAM for the opt-in real-data parity check. The data is
never committed. Example:

  scripts/fetch-real-data.sh \
    https://example.org/small.bam /tmp/husk-real/sample.bam
USAGE
    exit 2
fi

url=$1
destination=${2:-"${TMPDIR:-/tmp}/husk-real-data/sample.bam"}

if ! command -v samtools >/dev/null 2>&1; then
    echo "fetch-real-data: samtools is required" >&2
    exit 1
fi

mkdir -p "$(dirname "${destination}")"
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "${url}" -o "${destination}"
else
    wget -q "${url}" -O "${destination}"
fi

samtools quickcheck -v "${destination}" || {
    echo "fetch-real-data: downloaded file is not a valid BAM" >&2
    rm -f "${destination}"
    exit 1
}
echo "${destination}"
