#!/bin/bash
# Build cdae_native as a Python wheel for the current platform.
# Uses uv to manage Python versions — can download standalone Python builds
# matching Blender's ABI without needing system packages.
#
# Usage:
#   ./build.sh                    # Build with system Python (via uv venv)
#   ./build.sh --blender-python   # Build using Blender's Python version (via uv)
#   BLENDER_PY_VER=3.13 ./build.sh --blender-python  # Manual version override
#
# Wheels are output directly to ../io_beamng_dae/wheels/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Determine target Python version
if [ "$1" = "--blender-python" ]; then
    if [ -z "$BLENDER_PY_VER" ]; then
        echo "Detecting Blender's Python ABI version..."
        # Blender's extension system checks the compile-time Python ABI, not the
        # runtime version. On Arch, Blender may run against Python 3.14 but its
        # extension system was compiled for 3.13. We detect the ABI from the
        # extension suffix that Blender's importlib uses.
        BLENDER_PY_VER=$(blender --background --python-expr "
import importlib.machinery
suffix = importlib.machinery.EXT_SUFFIX
# Extract version from e.g. '.cpython-313-x86_64-linux-gnu.so'
import re
m = re.search(r'cpython-(\d+)(\d+)', suffix)
if m:
    print('PYVER:' + m.group(1) + '.' + m.group(2))
else:
    # Fallback to sys.version_info
    import sys
    print('PYVER:' + '.'.join(str(v) for v in sys.version_info[:2]))
" 2>/dev/null | grep -oP '(?<=PYVER:).*' | head -1)

        if [ -z "$BLENDER_PY_VER" ]; then
            echo "Error: Could not detect Blender's Python version. Is blender on PATH?"
            echo "Set manually: BLENDER_PY_VER=3.13 ./build.sh --blender-python"
            exit 1
        fi
    fi
    echo "Blender extension ABI: Python $BLENDER_PY_VER"
    TARGET_PY="$BLENDER_PY_VER"
else
    echo "Building with system Python..."
    TARGET_PY=""  # Let uv pick the default
fi

# Use uv to create a venv with the exact Python version
VENV_DIR="$SCRIPT_DIR/.build_venv"
echo "Creating uv venv at $VENV_DIR (Python $TARGET_PY)..."
if [ -n "$TARGET_PY" ]; then
    uv venv "$VENV_DIR" --python "$TARGET_PY"
else
    uv venv "$VENV_DIR"
fi
source "$VENV_DIR/bin/activate"

echo "Installing build dependencies via uv..."
uv pip install build pybind11 cmake scikit-build-core

echo "Building wheel..."
python -m build --wheel --outdir "$SCRIPT_DIR/../io_beamng_dae/wheels"

# Deactivate and clean up venv
deactivate
rm -rf "$VENV_DIR"

echo ""
echo "Build complete. Wheel(s) in ../io_beamng_dae/wheels/:"
ls -la "$SCRIPT_DIR/../io_beamng_dae/wheels/"cdae_native-*.whl

# Update blender_manifest.toml with built wheel(s) and platform(s)
echo ""
echo "Updating blender_manifest.toml..."
python3 "$SCRIPT_DIR/update_manifest.py"
