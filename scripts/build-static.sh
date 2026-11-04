#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(awk '/^[[:space:]]*VERSION[[:space:]]/ {print $2; exit}' \
    "${root}/CMakeLists.txt")
htslib_version=${HTSLIB_VERSION:-1.25}
htslib_prefix=${HTSLIB_PREFIX:-"${root}/.cache/htslib-${htslib_version}"}
build_dir=${BUILD_DIR:-"${root}/build-static"}
dist_dir=${DIST_DIR:-"${root}/dist"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "build-static: this script targets Linux" >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "build-static: cmake is required" >&2
    exit 1
fi
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
    echo "build-static: curl or wget is required" >&2
    exit 1
fi

if [[ ! -f "${htslib_prefix}/lib/libhts.a" ]]; then
    cache="${root}/.cache"
    mkdir -p "${cache}"
    tarball="${cache}/htslib-${htslib_version}.tar.bz2"
    if [[ ! -f "${tarball}" ]]; then
        url="https://github.com/samtools/htslib/releases/download/${htslib_version}/htslib-${htslib_version}.tar.bz2"
        if command -v curl >/dev/null 2>&1; then
            curl -fsSL "${url}" -o "${tarball}"
        else
            wget -q "${url}" -O "${tarball}"
        fi
    fi

    source_dir="${cache}/htslib-${htslib_version}"
    rm -rf "${source_dir}"
    mkdir -p "${source_dir}"
    tar -xjf "${tarball}" -C "${source_dir}" --strip-components=1
    (
        cd "${source_dir}"
        ./configure \
            --prefix="${htslib_prefix}" \
            --disable-bz2 \
            --disable-lzma \
            --disable-libcurl \
            --without-libdeflate
        make -j "${jobs}"
        make install
    )
fi

export PKG_CONFIG_PATH="${htslib_prefix}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
rm -rf "${build_dir}"
cmake -S "${root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DHUSK_STATIC=ON \
    -DHUSK_FULLY_STATIC=ON \
    -DCMAKE_PREFIX_PATH="${htslib_prefix}"
cmake --build "${build_dir}" --parallel "${jobs}"

binary="${build_dir}/husk"
if ldd "${binary}" 2>&1 | grep -q 'libhts'; then
    echo "build-static: binary still depends on libhts" >&2
    exit 1
fi

mkdir -p "${dist_dir}"
arch=$(uname -m)
case "${arch}" in
    x86_64|amd64) arch=x86_64 ;;
    aarch64|arm64) arch=aarch64 ;;
esac
output="${dist_dir}/husk-${version}-linux-${arch}"
cp "${binary}" "${output}"
if command -v strip >/dev/null 2>&1; then
    strip "${output}"
fi
echo "${output}"
