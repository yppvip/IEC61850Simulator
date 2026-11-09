#!/usr/bin/env bash

# Build the Debug configuration from an MSYS2 UCRT64 terminal.
set -euo pipefail

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    echo "Please run this script from an MSYS2 UCRT64 terminal (MSYSTEM=UCRT64)." >&2
    exit 1
fi

if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
    echo "CMake and Ninja must be installed in the MSYS2 UCRT64 environment." >&2
    echo "Run: pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base" >&2
    exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${source_dir}/build/windows-ucrt64-debug"

cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=/ucrt64
cmake --build "${build_dir}"

echo "Build and Qt runtime deployment completed: ${build_dir}/bin/IEC61850Simulator.exe"
