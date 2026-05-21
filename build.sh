#!/usr/bin/env bash
# Local build (macOS / Linux). Mirror of build.ps1.
#
# Usage:
#   ./build.sh                       build for host triple (auto-detected)
#   ./build.sh --preset linux-x64    explicit preset
#   ./build.sh --configure           force re-run cmake configure
#   ./build.sh --clean               wipe the preset's build dir and exit

set -euo pipefail
cd "$(dirname "$0")"

PRESET=""
CLEAN=0
CONFIGURE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)    PRESET="$2"; shift 2 ;;
        --clean)     CLEAN=1; shift ;;
        --configure) CONFIGURE=1; shift ;;
        -h|--help)
            sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

# Auto-detect preset from host when not given.
if [[ -z "$PRESET" ]]; then
    case "$(uname -s)" in
        Linux)  PRESET="linux-x64" ;;
        Darwin)
            if [[ "$(uname -m)" == "arm64" ]]; then PRESET="darwin-arm64"
            else                                    PRESET="darwin-x64"
            fi
            ;;
        *) echo "unsupported host: $(uname -s)" >&2; exit 2 ;;
    esac
fi

BUILD_DIR="build/$PRESET"

if [[ "$CLEAN" -eq 1 ]]; then
    rm -rf "$BUILD_DIR"
    echo "cleaned $BUILD_DIR"
    exit 0
fi

if [[ "$CONFIGURE" -eq 1 || ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "configuring preset: $PRESET"
    cmake --preset "$PRESET"
fi

echo "building: $PRESET"
cmake --build "$BUILD_DIR" --config Release

echo "built: $BUILD_DIR"
ls -lh "$BUILD_DIR"/*.{so,dylib} 2>/dev/null || true
