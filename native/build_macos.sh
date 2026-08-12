#!/bin/bash
# Build cdae_native macOS wheels.
#
# BeamNG.drive has no macOS binaries, so macOS wheels are not built by default.
# This script is provided for community members who want macOS support.
#
# Prerequisites:
#   - Xcode Command Line Tools: xcode-select --install
#   - uv:  https://docs.astral.sh/uv/getting-started/installation/
#   - cmake (uv will install it in the venv if not on PATH)
#
# Usage:
#   ./build_macos.sh                         # Build for current arch (arm64 or x86_64)
#   ./build_macos.sh --blender-python        # Build using Blender's Python ABI version
#   ./build_macos.sh --arch arm64            # Build for Apple Silicon only
#   ./build_macos.sh --arch x86_64           # Build for Intel only
#   ./build_macos.sh --arch universal2       # Build universal2 (arm64 + x86_64)
#   BLENDER_PY_VER=3.13 ./build_macos.sh --blender-python --arch arm64
#
# Wheels are output to ../io_beamng_dae/wheels/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Defaults
ARCH=""
BLENDER_PY=""

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="$2"
            shift 2
            ;;
        --blender-python)
            BLENDER_PY="1"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Default arch: current host architecture
if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
fi

# Set deployment target (affects wheel platform tag)
# arm64 requires 11.0+, x86_64 can go back to 10.9
if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "universal2" ]; then
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
else
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-10.9}"
fi

echo "Building for macOS ($ARCH), deployment target: $MACOSX_DEPLOYMENT_TARGET"

# Map arch to CMAKE_OSX_ARCHITECTURES
case "$ARCH" in
    arm64)      CMAKE_ARCH="arm64" ;;
    x86_64)     CMAKE_ARCH="x86_64" ;;
    universal2) CMAKE_ARCH="arm64;x86_64" ;;
    *) echo "Error: Unknown arch '$ARCH' (use arm64, x86_64, or universal2)"; exit 1 ;;
esac

# Determine target Python version
if [ -n "$BLENDER_PY" ]; then
    if [ -z "$BLENDER_PY_VER" ]; then
        echo "Detecting Blender's Python ABI version..."
        BLENDER_PY_VER=$(blender --background --python-expr "
import importlib.machinery, re, sys
suffix = importlib.machinery.EXT_SUFFIX
m = re.search(r'cpython-(\d+)(\d+)', suffix)
if m:
    print('PYVER:' + m.group(1) + '.' + m.group(2))
else:
    print('PYVER:' + '.'.join(str(v) for v in sys.version_info[:2]))
" 2>/dev/null | grep -oP '(?<=PYVER:).*' | head -1)

        if [ -z "$BLENDER_PY_VER" ]; then
            echo "Error: Could not detect Blender's Python version. Is blender on PATH?"
            echo "Set manually: BLENDER_PY_VER=3.13 ./build_macos.sh --blender-python"
            exit 1
        fi
    fi
    echo "Blender extension ABI: Python $BLENDER_PY_VER"
    TARGET_PY="$BLENDER_PY_VER"
else
    echo "Building with system Python..."
    TARGET_PY=""
fi

# Create venv
VENV_DIR="$SCRIPT_DIR/.build_venv_macos"
echo "Creating uv venv at $VENV_DIR (Python $TARGET_PY)..."
if [ -n "$TARGET_PY" ]; then
    uv venv "$VENV_DIR" --python "$TARGET_PY"
else
    uv venv "$VENV_DIR"
fi
source "$VENV_DIR/bin/activate"

echo "Installing build dependencies via uv..."
uv pip install build pybind11 cmake scikit-build-core

# Build with macOS-specific CMake flags
echo "Building wheel..."
export CMAKE_OSX_ARCHITECTURES="$CMAKE_ARCH"
python -m build --wheel --outdir "$SCRIPT_DIR/../io_beamng_dae/wheels"

# Deactivate and clean up
deactivate
rm -rf "$VENV_DIR"

echo ""
echo "Build complete. Wheel(s) in ../io_beamng_dae/wheels/:"
ls -la "$SCRIPT_DIR/../io_beamng_dae/wheels/"cdae_native-*macosx*.whl 2>/dev/null || echo "No macOS wheels found."

# Update blender_manifest.toml with built wheel(s) and platform(s)
echo ""
echo "Updating blender_manifest.toml..."
python3 "$SCRIPT_DIR/update_manifest.py"
