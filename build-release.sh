#!/usr/bin/env bash
# Clean release build: wipe build-release/, reconfigure with HIGHLINE_RELEASE=ON, make.
# Usage:
#   ./build-release.sh
#   ./build-release.sh --with-scene-editor
#   ./build-release.sh --with-dev-tools
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/build-release"
WITH_SCENE_EDITOR=0
WITH_DEV_TOOLS=0

usage() {
    cat <<'EOF'
Usage: ./build-release.sh [options]

Wipe and recreate build-release/, configure a release (embedded resources)
build, then compile with make -j.

Options:
  --with-scene-editor   Also build ./scene-editor (HIGHLINE_BUILD_EDITOR=ON)
  --with-dev-tools      Keep in-game developer tools (HIGHLINE_DEV_TOOLS=ON)
                        Ctrl+Shift+S overlay and ~ give-item console.
                        Omitted by default for release builds.
  -h, --help            Show this help

Binaries land in build-release/:
  ./Highline\ Ridge
  ./scene-editor          (only with --with-scene-editor)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-scene-editor)
            WITH_SCENE_EDITOR=1
            shift
            ;;
        --with-dev-tools)
            WITH_DEV_TOOLS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

CMAKE_ARGS=(-DHIGHLINE_RELEASE=ON)
SUMMARY="game only"
if [[ "${WITH_SCENE_EDITOR}" -eq 1 ]]; then
    CMAKE_ARGS+=(-DHIGHLINE_BUILD_EDITOR=ON)
    SUMMARY="${SUMMARY} + scene editor"
fi
if [[ "${WITH_DEV_TOOLS}" -eq 1 ]]; then
    CMAKE_ARGS+=(-DHIGHLINE_DEV_TOOLS=ON)
    SUMMARY="${SUMMARY} + dev tools"
fi
echo "==> Release build (${SUMMARY})"
echo "    Tips: --with-scene-editor, --with-dev-tools"

echo "==> Removing ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

echo "==> cmake ${CMAKE_ARGS[*]}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"

echo "==> make -j${JOBS}"
make -C "${BUILD_DIR}" -j"${JOBS}"

echo
echo "Done. Run from ${BUILD_DIR}:"
echo "  ./Highline\\ Ridge"
if [[ "${WITH_SCENE_EDITOR}" -eq 1 ]]; then
    echo "  ./scene-editor"
fi
if [[ "${WITH_DEV_TOOLS}" -eq 1 ]]; then
    echo "  Dev tools: Ctrl+Shift+S (overlay), ~ (command console, give-item)"
fi

# -----------------------------------------------------------------------------
# Runtime debug (not a CMake flag): HIGHLINE_SYNC_SCENE_LOAD
#
# By default the game decodes scene images (xz decompress + PNG/JPEG) on a
# background JobSystem worker, keeps drawing the previous room, then uploads
# the new texture on the main/GL thread when ready. That cuts transition
# hitches. Movement is blocked briefly while the load is pending.
#
# Set this environment variable to exactly "1" to force the old synchronous
# path (decode + GPU upload on the main thread before the next frame). Useful
# when debugging wrong/blank scene art, JobSystem issues, or comparing hitch
# timing. Unset / any other value keeps async loading.
#
# Example (from build-release/):
#   HIGHLINE_SYNC_SCENE_LOAD=1 ./Highline\ Ridge
#
# export HIGHLINE_SYNC_SCENE_LOAD=1
#
# Audio beds currently always load synchronously (file /tmp extract path).
# Async JobSystem audio is disabled until onRoomEnter races are fully fixed.
# -----------------------------------------------------------------------------
