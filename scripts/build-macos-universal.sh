#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(awk '/^[[:space:]]*VERSION[[:space:]]/ {print $2; exit}' \
    "${root}/CMakeLists.txt")
htslib_version=${HTSLIB_VERSION:-1.25}
htslib_prefix=${HTSLIB_PREFIX:-"${root}/.cache/htslib-macos-${htslib_version}"}
build_dir=${BUILD_DIR:-"${root}/build-macos"}
dist_dir=${DIST_DIR:-"${root}/dist"}
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}
deployment_target=${MACOSX_DEPLOYMENT_TARGET:-11.0}

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build-macos-universal: this script targets macOS" >&2
    exit 1
fi

if [[ ! -f "${htslib_prefix}/lib/libhts.a" ]]; then
    cache="${root}/.cache"
    mkdir -p "${cache}"
    tarball="${cache}/htslib-${htslib_version}.tar.bz2"
    if [[ ! -f "${tarball}" ]]; then
        curl -fsSL \
            "https://github.com/samtools/htslib/releases/download/${htslib_version}/htslib-${htslib_version}.tar.bz2" \
            -o "${tarball}"
    fi
    source_dir="${cache}/htslib-${htslib_version}-macos"
    rm -rf "${source_dir}"
    mkdir -p "${source_dir}"
    tar -xjf "${tarball}" -C "${source_dir}" --strip-components=1
    (
        cd "${source_dir}"
        export MACOSX_DEPLOYMENT_TARGET="${deployment_target}"
        export CFLAGS="-arch x86_64 -arch arm64 -O2"
        export CXXFLAGS="${CFLAGS}"
        export LDFLAGS="-arch x86_64 -arch arm64"
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
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${deployment_target}" \
    -DCMAKE_PREFIX_PATH="${htslib_prefix}"
cmake --build "${build_dir}" --parallel "${jobs}"

binary="${build_dir}/husk"
if ! lipo -info "${binary}" | grep -q 'x86_64' ||
    ! lipo -info "${binary}" | grep -q 'arm64'; then
    echo "build-macos-universal: binary is not universal2" >&2
    lipo -info "${binary}" >&2
    exit 1
fi

if [[ -n "${MACOS_SIGN_IDENTITY:-}" ]]; then
    codesign --force --options runtime --timestamp \
        --sign "${MACOS_SIGN_IDENTITY}" "${binary}"
else
    codesign --force --sign - "${binary}"
fi

mkdir -p "${dist_dir}"
output="${dist_dir}/husk-${version}-macos-universal2"
cp "${binary}" "${output}"
echo "${output}"
