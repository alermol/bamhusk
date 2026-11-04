#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(awk '/^[[:space:]]*VERSION[[:space:]]/ {print $2; exit}' \
    "${root}/CMakeLists.txt")
htslib_version=${HTSLIB_VERSION:-1.25}
htslib_prefix=${HTSLIB_PREFIX:-"${root}/.cache/htslib-windows-${htslib_version}"}
build_dir=${BUILD_DIR:-"${root}/build-windows"}
dist_dir=${DIST_DIR:-"${root}/dist"}
jobs=${JOBS:-$(nproc 2>/dev/null || echo 4)}
mingw_prefix=${MINGW_PREFIX:-/mingw64}
mingw_cmake_prefix=$(cygpath -m "${mingw_prefix}" 2>/dev/null || \
    printf '%s' "${mingw_prefix}")
htslib_cmake_prefix=$(cygpath -m "${htslib_prefix}" 2>/dev/null || \
    printf '%s' "${htslib_prefix}")

case "$(uname -s)" in
    MINGW*|MSYS*) ;;
    *)
        echo "build-windows: run this script from an MSYS2 MinGW shell" >&2
        exit 1
        ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
    echo "build-windows: cmake is required" >&2
    exit 1
fi

if [[ ! -f "${htslib_prefix}/lib/libhts.a" ]]; then
    for command in make tar; do
        if ! command -v "${command}" >/dev/null 2>&1; then
            echo "build-windows: ${command} is required" >&2
            exit 1
        fi
    done

    cache="${root}/.cache"
    mkdir -p "${cache}"
    tarball="${cache}/htslib-${htslib_version}.tar.bz2"
    if [[ ! -f "${tarball}" ]]; then
        if ! command -v curl >/dev/null 2>&1 &&
            ! command -v wget >/dev/null 2>&1; then
            echo "build-windows: curl or wget is required" >&2
            exit 1
        fi
        url="https://github.com/samtools/htslib/releases/download/${htslib_version}/htslib-${htslib_version}.tar.bz2"
        if command -v curl >/dev/null 2>&1; then
            curl -fsSL "${url}" -o "${tarball}"
        else
            wget -q "${url}" -O "${tarball}"
        fi
    fi

    source_dir="${cache}/htslib-${htslib_version}-windows"
    rm -rf "${source_dir}"
    mkdir -p "${source_dir}"
    tar -xjf "${tarball}" -C "${source_dir}" --strip-components=1
    (
        cd "${source_dir}"
        # libsystre provides the POSIX regex API through TRE; keeping it out
        # of the htslib pkg-config file means CMake must add it explicitly.
        LIBS="-lregex -ltre -lintl ${LIBS:-}" \
        ./configure \
            --prefix="${htslib_cmake_prefix}" \
            --disable-bz2 \
            --disable-lzma \
            --disable-libcurl \
            --disable-gcs \
            --disable-s3 \
            --without-libdeflate
        # Husk only needs the static archive. Avoid the shared DLL target,
        # which requires gendef on MinGW.
        make -j "${jobs}" libhts.a
        make installdirs install-pkgconfig
        cp libhts.a "${htslib_prefix}/lib/libhts.a"
        cp htslib/*.h "${htslib_prefix}/include/htslib/"
    )
fi

export PKG_CONFIG_PATH="${htslib_prefix}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
rm -rf "${build_dir}"
cmake -S "${root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DHUSK_STATIC=ON \
    -DHUSK_FULLY_STATIC=ON \
    -DCMAKE_PREFIX_PATH="${htslib_cmake_prefix};${mingw_cmake_prefix}"
cmake --build "${build_dir}" --parallel "${jobs}"

mkdir -p "${dist_dir}"
output="${dist_dir}/husk-${version}-windows-x86_64.exe"
cp "${build_dir}/husk.exe" "${output}"
echo "${output}"
