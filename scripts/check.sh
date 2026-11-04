#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "${root}"

cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
