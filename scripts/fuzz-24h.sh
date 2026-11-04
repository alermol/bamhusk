#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
seconds=${FUZZ_SECONDS:-86400}
build=${BUILD_DIR:-"${root}/build-fuzz"}
artifacts=${ARTIFACT_DIR:-"${root}/fuzz-artifacts"}

if ! command -v clang++ >/dev/null 2>&1; then
    echo "fuzz-24h: clang++ is required" >&2
    exit 1
fi

cmake -S "${root}" -B "${build}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DHUSK_BUILD_FUZZERS=ON \
    -DHUSK_SANITIZE=ON \
    -DBUILD_TESTING=OFF
cmake --build "${build}" --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

mkdir -p "${artifacts}" "${root}/fuzz-corpus/cli" "${root}/fuzz-corpus/header" "${root}/fuzz-corpus/bam"
if command -v samtools >/dev/null 2>&1; then
    "${root}/scripts/make-fixtures.sh" "${root}/.cache/fuzz-fixtures" >/dev/null
    cp "${root}/.cache/fuzz-fixtures/"*.bam "${root}/fuzz-corpus/bam/"
fi
export ASAN_OPTIONS=detect_leaks=0

"${build}/fuzz_cli" \
    -max_total_time="${seconds}" -max_len=512 \
    -artifact_prefix="${artifacts}/cli-" "${root}/fuzz-corpus/cli" &
cli_pid=$!
"${build}/fuzz_header" \
    -max_total_time="${seconds}" -max_len=4096 \
    -artifact_prefix="${artifacts}/header-" "${root}/fuzz-corpus/header" &
header_pid=$!
"${build}/fuzz_bam" \
    -max_total_time="${seconds}" -max_len=65536 \
    -artifact_prefix="${artifacts}/bam-" "${root}/fuzz-corpus/bam" &
bam_pid=$!

status=0
wait "${cli_pid}" || status=$?
wait "${header_pid}" || status=$?
wait "${bam_pid}" || status=$?
exit "${status}"
