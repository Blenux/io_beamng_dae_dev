#!/bin/bash
# Build cdae_native as Python wheels for all supported Blender versions.
# Uses uv to manage Python versions — can download standalone Python builds
# matching Blender's ABI without needing system packages.
#
# Blender extension Python ABI versions:
#   3.11  — Blender 5.0.1
#   3.13  — Blender 5.2.0+
#
# Usage:
#   ./build.sh                # Build for all supported Blender Python versions
#   ./build.sh 3.13           # Build for specific version only
#   ./build.sh 3.11 3.13      # Build for specific versions
#
# Wheels are output directly to ../io_beamng_dae/wheels/

set -e

# Supported Blender extension Python ABI versions
SUPPORTED_PY_VERSIONS="3.11 3.13"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Determine which Python versions to build for
if [ $# -gt 0 ]; then
    PY_VERSIONS="$@"
else
    PY_VERSIONS="$SUPPORTED_PY_VERSIONS"
fi

echo "Building cdae_native wheels for Python: $PY_VERSIONS"
echo ""

# Build for each Python version
for PYVER in $PY_VERSIONS; do
    echo "============================================"
    echo "Building for Python $PYVER"
    echo "============================================"

    # Use uv to create a venv with the exact Python version
    VENV_DIR="$SCRIPT_DIR/.build_venv_$PYVER"
    echo "Creating uv venv at $VENV_DIR (Python $PYVER)..."
    uv venv "$VENV_DIR" --python "$PYVER"
    source "$VENV_DIR/bin/activate"

    echo "Installing build dependencies via uv..."
    uv pip install build pybind11 cmake scikit-build-core

    echo "Building wheel..."
    python -m build --wheel --outdir "$SCRIPT_DIR/../io_beamng_dae/wheels"

    # Deactivate and clean up venv
    deactivate
    rm -rf "$VENV_DIR"
    echo ""
done

echo "Build complete. Wheel(s) in ../io_beamng_dae/wheels/:"
ls -la "$SCRIPT_DIR/../io_beamng_dae/wheels/"cdae_native-*.whl

# Update blender_manifest.toml with built wheel(s) and platform(s)
echo ""
echo "Updating blender_manifest.toml..."
python3 "$SCRIPT_DIR/update_manifest.py"
