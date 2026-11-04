#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(awk '/^[[:space:]]*VERSION[[:space:]]/ {print $2; exit}' \
    "${root}/CMakeLists.txt")
build_dir=${BUILD_DIR:-"${root}/build-windows"}
dist_dir=${DIST_DIR:-"${root}/dist"}
jobs=${JOBS:-$(nproc 2>/dev/null || echo 4)}
mingw_prefix=${MINGW_PREFIX:-/mingw64}
mingw_cmake_prefix=$(cygpath -m "${mingw_prefix}" 2>/dev/null || \
    printf '%s' "${mingw_prefix}")

case "$(uname -s)" in
    MINGW*|MSYS*) ;;
    *)
        echo "build-windows: run this script from an MSYS2 MinGW shell" >&2
        exit 1
        ;;
esac

export PKG_CONFIG_PATH="${mingw_prefix}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
rm -rf "${build_dir}"
cmake -S "${root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DHUSK_STATIC=ON \
    -DHUSK_FULLY_STATIC=ON \
    -DCMAKE_PREFIX_PATH="${mingw_cmake_prefix}"
cmake --build "${build_dir}" --parallel "${jobs}"

mkdir -p "${dist_dir}"
output="${dist_dir}/husk-${version}-windows-x86_64.exe"
cp "${build_dir}/husk.exe" "${output}"
echo "${output}"
