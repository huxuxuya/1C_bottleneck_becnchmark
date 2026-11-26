#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET="${1:-}"
IMAGE="${NATIVE_BUILD_IMAGE:-1c-bottleneck-native-builder:bookworm}"
SDK_INCLUDE="${NATIVEAPI_INCLUDE_DIR:-}"
DOCKER_PLATFORM="${NATIVE_DOCKER_PLATFORM:-}"

if [[ -z "$TARGET" ]]; then
    echo "Usage: $0 <core|linux-i386|linux-x86_64|linux-arm64|windows-i386|windows-x86_64|macos-x86_64>" >&2
    exit 2
fi

BUILD_DIR="build/native/${TARGET}"
SOURCE_DIR="native/cpu-frequency"
TOOLCHAIN=""

case "$TARGET" in
    core)
        ;;
    linux-i386|linux-x86_64|linux-arm64|windows-i386|windows-x86_64)
        if [[ -z "$SDK_INCLUDE" || ! -f "$SDK_INCLUDE/ComponentBase.h" ]]; then
            echo "Set NATIVEAPI_INCLUDE_DIR to the official 1C VNCOMP83/include directory." >&2
            exit 2
        fi
        case "$TARGET" in
            linux-i386) TOOLCHAIN="linux-i386" ;;
            linux-x86_64) TOOLCHAIN="linux-x86_64" ;;
            linux-arm64) TOOLCHAIN="linux-arm64" ;;
            windows-i386) TOOLCHAIN="windows-i386" ;;
            windows-x86_64) TOOLCHAIN="windows-x86_64" ;;
        esac
        ;;
    macos-x86_64)
        if [[ "$(uname -s)" != "Darwin" ]]; then
            echo "macOS binaries must be built with Apple's SDK on a macOS host." >&2
            exit 2
        fi
        if [[ -z "$SDK_INCLUDE" || ! -f "$SDK_INCLUDE/ComponentBase.h" ]]; then
            echo "Set NATIVEAPI_INCLUDE_DIR to the official 1C VNCOMP83/include directory." >&2
            exit 2
        fi
        mkdir -p "$ROOT/$BUILD_DIR"
        cmake -S "$ROOT/$SOURCE_DIR" -B "$ROOT/$BUILD_DIR" \
            -DBUILD_NATIVEAPI_COMPONENT=ON \
            -DBUILD_BENCHMARK_CLI=OFF \
            -DONEC_NATIVEAPI_INCLUDE_DIR="$SDK_INCLUDE" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES=x86_64
        cmake --build "$ROOT/$BUILD_DIR" --config Release
        exit 0
        ;;
    e2k)
        echo "E2K requires the Elbrus cross-compiler and sysroot; use an E2K-capable build runner." >&2
        exit 2
        ;;
    *)
        echo "Unsupported target: $TARGET" >&2
        exit 2
        ;;
esac

docker build -f "$ROOT/docker/native-builder/Dockerfile" -t "$IMAGE" "$ROOT/docker/native-builder"

mkdir -p "$ROOT/$BUILD_DIR"
docker_args=(run --rm -v "$ROOT:/workspace" -w /workspace)
if [[ -n "$DOCKER_PLATFORM" ]]; then
    docker_args+=(--platform "$DOCKER_PLATFORM")
fi
if [[ "$TARGET" != "core" ]]; then
    docker_args+=(-v "$SDK_INCLUDE:/opt/1c-native-api/include:ro")
fi

cmake_args=(
    -S "/workspace/$SOURCE_DIR"
    -B "/workspace/$BUILD_DIR"
    -G Ninja
    -DBUILD_NATIVEAPI_COMPONENT=$([[ "$TARGET" == "core" ]] && echo OFF || echo ON)
    -DBUILD_BENCHMARK_CLI=$([[ "$TARGET" == "core" ]] && echo ON || echo OFF)
    -DCMAKE_BUILD_TYPE=Release
)
if [[ "$TARGET" != "core" ]]; then
    cmake_args+=(-DONEC_NATIVEAPI_INCLUDE_DIR=/opt/1c-native-api/include)
    cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="/workspace/$SOURCE_DIR/cmake/toolchains/${TOOLCHAIN}.cmake")
fi

docker "${docker_args[@]}" "$IMAGE" cmake "${cmake_args[@]}"
docker "${docker_args[@]}" "$IMAGE" cmake --build "/workspace/$BUILD_DIR" --config Release
