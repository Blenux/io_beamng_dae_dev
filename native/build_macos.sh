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
#   ./build_macos.sh                         # Build for all supported Blender Python versions, current arch
#   ./build_macos.sh --arch arm64            # Build for Apple Silicon only
#   ./build_macos.sh --arch x86_64           # Build for Intel only
#   ./build_macos.sh --arch universal2       # Build universal2 (arm64 + x86_64)
#   ./build_macos.sh 3.13 --arch arm64       # Build for specific Python version only
#   ./build_macos.sh 3.11 3.13 --arch arm64  # Build for specific versions
#
# Wheels are output to ../io_beamng_dae/wheels/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Defaults
ARCH=""

# Supported Blender extension Python ABI versions
SUPPORTED_PY_VERSIONS="3.11 3.13"

# Parse args — separate --arch flags from Python version args
PY_VERSIONS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="$2"
            shift 2
            ;;
        *)
            PY_VERSIONS="$PY_VERSIONS $1"
            shift
            ;;
    esac
done

# Default Python versions if none specified
if [ -z "$PY_VERSIONS" ]; then
    PY_VERSIONS="$SUPPORTED_PY_VERSIONS"
fi

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

# Always use Blender's Python ABI version (unless manually overridden)
echo "Building for macOS ($ARCH), Python: $PY_VERSIONS"
echo "Deployment target: $MACOSX_DEPLOYMENT_TARGET"
echo ""

# Build for each Python version
for PYVER in $PY_VERSIONS; do
    echo "============================================"
    echo "Building for Python $PYVER (macOS $ARCH)"
    echo "============================================"

    # Create venv
    VENV_DIR="$SCRIPT_DIR/.build_venv_macos_$PYVER"
    echo "Creating uv venv at $VENV_DIR (Python $PYVER)..."
    uv venv "$VENV_DIR" --python "$PYVER"
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
done

echo ""
echo "Build complete. Wheel(s) in ../io_beamng_dae/wheels/:"
ls -la "$SCRIPT_DIR/../io_beamng_dae/wheels/"cdae_native-*macosx*.whl 2>/dev/null || echo "No macOS wheels found."

# Update blender_manifest.toml with built wheel(s) and platform(s)
echo ""
echo "Updating blender_manifest.toml..."
python3 "$SCRIPT_DIR/update_manifest.py"
