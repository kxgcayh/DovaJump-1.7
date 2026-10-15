#!/usr/bin/env bash
#
# DovaJump Release Build and Packaging Script
#
# Usage:
#   ./package-release.sh [options]
#
# Options:
#   --skip-build     Skip compilation step and package existing DLL
#   --ng-only        Package only the NG Patch archive
#   --aio-only       Package only the All-In-One archive
#   --help           Show this help message
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SKIP_BUILD=0
EXTRA_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --skip-build)
            SKIP_BUILD=1
            ;;
        --ng-only)
            EXTRA_ARGS+=("--ng-only")
            ;;
        --aio-only)
            EXTRA_ARGS+=("--aio-only")
            ;;
        -h|--help)
            echo "Usage: $0 [--skip-build] [--ng-only] [--aio-only]"
            exit 0
            ;;
        *)
            EXTRA_ARGS+=("$arg")
            ;;
    esac
done

# Build step if not skipped and cross-compilation environment detected
if [[ $SKIP_BUILD -eq 0 ]]; then
    BUILD_DIR="$SCRIPT_DIR/build"
    TOOLCHAIN="${CMAKE_TOOLCHAIN_FILE:-/Volumes/S512/Projects/ETC/winsdk/toolchain/clang-cl-msvc.cmake}"
    VCPKG_PREFIX="${CMAKE_PREFIX_PATH:-/Volumes/S512/Projects/ETC/vcpkg/installed/x64-windows-cross}"

    if [[ -f "$TOOLCHAIN" ]]; then
        echo "[Build] Cross-compiling DovaJump via Clang-cl and MSVC sysroot..."
        cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
            -DCMAKE_PREFIX_PATH="$VCPKG_PREFIX" > /dev/null
        cmake --build "$BUILD_DIR" --config Release
    elif [[ -f "build/DovaJump.dll" || -f "out/build/x64-Release/DovaJump.dll" ]]; then
        echo "[Build] Existing binary found. Proceeding with packaging..."
    else
        echo "[Build] Building via standard CMake configuration..."
        cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$BUILD_DIR" --config Release
    fi
fi

# Run portable packager
if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    python3 "$SCRIPT_DIR/scripts/package-release.py" "${EXTRA_ARGS[@]}"
else
    python3 "$SCRIPT_DIR/scripts/package-release.py"
fi
