# Building Husk

## Requirements

| Dependency | Minimum | Notes |
|---|---|---|
| C++ compiler | GCC 13, Clang 18, MSVC 19.40 | C++23 |
| CMake | 3.28 | Presets require 3.28 |
| Ninja | any recent | Default generator |
| htslib | 1.21 | Build compatibility floor; release builds use 1.25 |
| samtools | 1.21 | Integration tests only |

## Quick start

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
./build/husk --help
```

`./scripts/check.sh` runs the same three commands.

## Presets

| Preset | Build directory | Notes |
|---|---|---|
| `default` | `build/` | Release |
| `debug` | `build-debug/` | Debug |

Both presets generate `compile_commands.json`.

## Finding htslib

`cmake/FindHTSlib.cmake` looks for pkg-config metadata first, then for
`htslib/sam.h` and `libhts`. Point CMake at a non-standard prefix with:

```sh
cmake --preset default -DCMAKE_PREFIX_PATH=/opt/htslib
```

If pkg-config is unavailable, install htslib in a standard prefix or pass
`CMAKE_PREFIX_PATH`.

## Static Linux build

`scripts/build-static.sh` builds htslib 1.25 from source with bundled
htscodecs, then links Husk fully statically:

```sh
JOBS=8 ./scripts/build-static.sh
file dist/husk-*-linux-x86_64
ldd dist/husk-*-linux-x86_64   # "not a dynamic executable"
```

The script caches htslib under `.cache/`. Set `HTSLIB_PREFIX` to reuse an
existing static htslib, `HTSLIB_VERSION` to change the version, and
`DIST_DIR`/`BUILD_DIR` to relocate outputs.

## macOS universal2 build

```sh
./scripts/build-macos-universal.sh
lipo -info dist/husk-*-macos-universal2
```

The script builds a universal htslib and a universal Husk binary. It ad-hoc
signs by default; set `MACOS_SIGN_IDENTITY` to sign with a Developer ID.
Notarisation runs in the release workflow when Apple secrets are configured.

## Windows build

Use an MSYS2 MinGW64 shell with `gcc`, `cmake`, `ninja`, `pkgconf`, `make`,
`bzip2`, `mingw-w64-x86_64-zlib`, `mingw-w64-x86_64-libiconv`,
`mingw-w64-x86_64-libsystre`, and `mingw-w64-x86_64-samtools` installed:

```sh
bash scripts/build-windows.sh
```

The script builds htslib 1.25 from source with libcurl/S3, bzip2, lzma, and
libdeflate disabled, then requests a fully static MinGW build. The release
workflow runs the same script under `msys2/setup-msys2`.

## Sanitizers and fuzzers

```sh
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DHUSK_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Build libFuzzer targets with:

```sh
cmake -S . -B build-fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DHUSK_BUILD_FUZZERS=ON \
  -DHUSK_SANITIZE=ON \
  -DBUILD_TESTING=OFF
cmake --build build-fuzz
```

`scripts/fuzz-24h.sh` runs all three fuzzers for 24 hours (override with
`FUZZ_SECONDS`).

## CMake options

| Option | Default | Purpose |
|---|---|---|
| `HUSK_STATIC` | `OFF` | Prefer `libhts.a` and static htslib dependencies |
| `HUSK_FULLY_STATIC` | `OFF` | Pass `-static` on Linux/MinGW |
| `HUSK_SANITIZE` | `OFF` | ASan + UBSan |
| `HUSK_BUILD_FUZZERS` | `OFF` | libFuzzer targets (Clang only) |
| `BUILD_TESTING` | `ON` | CTest unit/integration tests |

## Offline builds

Clone the repository, download the htslib 1.25 tarball into `.cache/`, and run
`HTSLIB_PREFIX=/path/to/static/htslib ./scripts/build-static.sh`. CMake does
not fetch dependencies at configure time.
