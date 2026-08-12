#!/bin/bash
# Cross-compile cdae_native Windows wheels from Linux using MinGW-w64.
#
# Downloads Windows Python standalone builds (for headers + import libs),
# uses MinGW-w64 to cross-compile the C++ extension via CMake, then
# packages the .pyd into a properly tagged win_amd64 wheel.
#
# Prerequisites:
#   - mingw-w64 cross-compiler (x86_64-w64-mingw32-g++)
#     Debian/Ubuntu: apt install mingw-w64
#     Arch:          pacman -S mingw-w64-gcc
#     Fedora:        dnf install mingw64-gcc-c++
#   - cmake >= 3.15
#   - uv (for venv management)
#   - curl, tar, git
#
# Usage:
#   ./build_windows.sh                # Build for all (3.11, 3.13)
#   ./build_windows.sh 3.11           # Build for specific version only
#   ./build_windows.sh 3.11 3.13      # Build for specific versions
#
# Wheels are output to ../io_beamng_dae/wheels/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PY_VERSIONS="${@:-3.11 3.13}"
WHEELS_DIR="$SCRIPT_DIR/../io_beamng_dae/wheels"
PY_STANDALONE_DIR="$SCRIPT_DIR/.py_standalone_win"

# Read package version from pyproject.toml (single source of truth)
NATIVE_VERSION=$(grep -oP '^version\s*=\s*"\K[^"]+' "$SCRIPT_DIR/pyproject.toml")
if [ -z "$NATIVE_VERSION" ]; then
    echo "Error: Could not extract version from pyproject.toml"
    exit 1
fi
DIST_INFO="cdae_native-${NATIVE_VERSION}.dist-info"

mkdir -p "$WHEELS_DIR" "$PY_STANDALONE_DIR"

# --- Check prerequisites ---
echo "Checking prerequisites..."

# Find MinGW C++ compiler (try common naming variants)
MINGW_CXX=""
for candidate in x86_64-w64-mingw32-g++-win32 x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++; do
    if command -v "$candidate" &>/dev/null; then
        MINGW_CXX="$candidate"
        break
    fi
done
if [ -z "$MINGW_CXX" ]; then
    echo "Error: MinGW-w64 C++ cross-compiler not found."
    echo "  Debian/Ubuntu: apt install mingw-w64"
    echo "  Arch:          pacman -S mingw-w64-gcc"
    echo "  Fedora:        dnf install mingw64-gcc-c++"
    exit 1
fi
MINGW_PREFIX="${MINGW_CXX%-g++*}"
echo "  MinGW C++: $MINGW_CXX"

for cmd in cmake curl tar git uv; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "Error: $cmd not found."
        exit 1
    fi
done
echo "  All prerequisites satisfied."

# --- Get latest python-build-standalone release tag + asset list ---
echo "Fetching latest python-build-standalone release..."
PBS_API_FILE="$SCRIPT_DIR/.pbs_release.json"
curl -sL "https://api.github.com/repos/astral-sh/python-build-standalone/releases/latest" -o "$PBS_API_FILE"
PBS_TAG=$(grep -oP '"tag_name":\s*"\K[^"]+' "$PBS_API_FILE")
if [ -z "$PBS_TAG" ]; then
    echo "Error: Could not fetch python-build-standalone release tag."
    rm -f "$PBS_API_FILE"
    exit 1
fi
echo "  Using release: $PBS_TAG"

# --- Download Windows Python standalone build ---
download_win_python() {
    local pyver="$1"
    local pyver_nodot="${pyver//./}"
    local target_dir="$PY_STANDALONE_DIR/$pyver"

    if [ -d "$target_dir/python" ]; then
        echo "  Windows Python $pyver already available."
        return 0
    fi

    # Query API for exact asset name
    local asset_name
    asset_name=$(grep -oP "\"name\":\\s*\"\\Kcpython-${pyver}\\.\\d+\\+${PBS_TAG}-x86_64-pc-windows-msvc-install_only\\.tar\\.gz" "$PBS_API_FILE" \
        | grep -v freethreaded \
        | head -1)
    if [ -z "$asset_name" ]; then
        echo "Error: No Windows Python $pyver asset found in release $PBS_TAG."
        return 1
    fi

    echo "  Downloading $asset_name..."
    local url="https://github.com/astral-sh/python-build-standalone/releases/download/${PBS_TAG}/${asset_name}"

    mkdir -p "$target_dir"
    local tmpfile="$target_dir/download.tar.gz"
    if ! curl -sL --fail "$url" -o "$tmpfile"; then
        echo "Error: Failed to download $url"
        rm -rf "$target_dir"
        return 1
    fi
    tar xzf "$tmpfile" -C "$target_dir"
    rm "$tmpfile"
}

# --- Create CMake toolchain file ---
create_toolchain() {
    local py_prefix="$1"
    local toolchain_file="$SCRIPT_DIR/.toolchain-win.cmake"

    cat > "$toolchain_file" << EOF
# Auto-generated MinGW-w64 cross-compilation toolchain
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER ${MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${MINGW_PREFIX}-windres)

# Windows Python extension suffix
set(CMAKE_SHARED_MODULE_SUFFIX ".pyd")

# Static-link MinGW runtimes (Blender's Windows Python is MSVC-built)
set(CMAKE_C_FLAGS_INIT "-static-libgcc -static-libstdc++ -static -lwinpthread")
set(CMAKE_CXX_FLAGS_INIT "-static-libgcc -static-libstdc++ -static -lwinpthread")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -static -lwinpthread")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -static -lwinpthread")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -static -lwinpthread")

# Search target root for libs/includes, host for packages (pybind11)
set(CMAKE_FIND_ROOT_PATH "${py_prefix}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF
    echo "$toolchain_file"
}

# --- Build for each Python version ---
for PYVER in $PY_VERSIONS; do
    PYVER_NODOT="${PYVER//./}"
    echo ""
    echo "============================================"
    echo "Cross-compiling for Windows Python $PYVER"
    echo "============================================"

    # Download Windows Python
    if ! download_win_python "$PYVER"; then
        echo "Skipping Python $PYVER..."
        continue
    fi

    # Locate Windows Python prefix, headers, and import library
    PY_PREFIX="$PY_STANDALONE_DIR/$PYVER/python"
    if [ ! -d "$PY_PREFIX" ]; then
        PY_PREFIX=$(find "$PY_STANDALONE_DIR/$PYVER" -name "python.exe" -exec dirname {} \; | head -1)
        if [ -z "$PY_PREFIX" ]; then
            echo "Error: Could not find Python prefix in $PY_STANDALONE_DIR/$PYVER"
            continue
        fi
    fi

    PY_INCLUDE="$PY_PREFIX/include"
    if [ ! -d "$PY_INCLUDE" ]; then
        PY_INCLUDE=$(find "$PY_PREFIX" -name "Python.h" -exec dirname {} \; | head -1)
        if [ -z "$PY_INCLUDE" ]; then
            echo "Error: Python headers not found in $PY_PREFIX"
            continue
        fi
    fi

    PY_LIB="$PY_PREFIX/libs/python$PYVER_NODOT.lib"
    if [ ! -f "$PY_LIB" ]; then
        PY_LIB=$(find "$PY_PREFIX" -name "python$PYVER_NODOT.lib" | head -1)
        if [ -z "$PY_LIB" ]; then
            echo "Error: python3$PYVER_NODOT.lib not found in $PY_PREFIX"
            continue
        fi
    fi

    echo "  Python prefix:  $PY_PREFIX"
    echo "  Include dir:    $PY_INCLUDE"
    echo "  Import library: $PY_LIB"

    # Create toolchain file
    TOOLCHAIN_FILE=$(create_toolchain "$PY_PREFIX")

    # Create venv for pybind11 CMake config (host Python, matching version)
    VENV_DIR="$SCRIPT_DIR/.build_venv_win_$PYVER"
    VENV_PY="$VENV_DIR/bin/python"
    echo "Creating build venv (host Python $PYVER)..."
    uv venv "$VENV_DIR" --python "$PYVER" 2>/dev/null || uv venv "$VENV_DIR"
    # Use uv pip install --python instead of activate
    uv pip install --python "$VENV_PY" pybind11

    PYBIND11_CMAKE_DIR=$("$VENV_PY" -c "import pybind11; print(pybind11.get_cmake_dir())")

    # Configure CMake
    BUILD_DIR="$SCRIPT_DIR/.build_win_$PYVER"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"

    # Set PYTHON_* cache variables for cross-compilation
    echo "Configuring CMake..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_BUILD_TYPE=Release \
        -Dpybind11_DIR="$PYBIND11_CMAKE_DIR" \
        -DCMAKE_DISABLE_FIND_PACKAGE_pugixml=ON \
        -DCMAKE_DISABLE_FIND_PACKAGE_zstd=ON \
        -DPYTHON_EXECUTABLE="$VENV_PY" \
        -DPYTHON_INCLUDE_DIR="$PY_INCLUDE" \
        -DPYTHON_LIBRARY="$PY_LIB" \
        -DPYTHON_INCLUDE_DIRS="$PY_INCLUDE" \
        -DPYTHON_LIBRARIES="$PY_LIB" \
        -DPYTHON_MODULE_EXTENSION=".cp${PYVER_NODOT}-win_amd64.pyd" \
        -DPYBIND11_FINDPYTHON=OFF

    echo "Building..."
    cmake --build "$BUILD_DIR" --config Release -j"$(nproc)"

    # Find the built extension (could be .pyd or .so depending on suffix detection)
    PYD_FILE=$(find "$BUILD_DIR" -name "cdae_native*.pyd" | head -1)
    if [ -z "$PYD_FILE" ]; then
        PYD_FILE=$(find "$BUILD_DIR" -name "cdae_native*.so" | head -1)
        if [ -n "$PYD_FILE" ]; then
            NEW_NAME="$(dirname "$PYD_FILE")/cdae_native.cp${PYVER_NODOT}-win_amd64.pyd"
            mv "$PYD_FILE" "$NEW_NAME"
            PYD_FILE="$NEW_NAME"
        fi
    fi

    if [ -z "$PYD_FILE" ]; then
        echo "Error: Extension module not found in build output."
        echo "Build directory contents:"
        find "$BUILD_DIR" -type f | head -20
        rm -rf "$VENV_DIR" "$BUILD_DIR"
        continue
    fi

    # Ensure correct filename with Windows ABI tag
    EXPECTED_NAME="cdae_native.cp${PYVER_NODOT}-win_amd64.pyd"
    ACTUAL_NAME=$(basename "$PYD_FILE")
    if [ "$ACTUAL_NAME" != "$EXPECTED_NAME" ]; then
        NEW_PATH="$(dirname "$PYD_FILE")/$EXPECTED_NAME"
        mv "$PYD_FILE" "$NEW_PATH"
        PYD_FILE="$NEW_PATH"
    fi

    echo "Built: $(basename "$PYD_FILE")"

    # Package into wheel
    echo "Packaging wheel..."
    WHEEL_NAME="cdae_native-${NATIVE_VERSION}-cp${PYVER_NODOT}-cp${PYVER_NODOT}-win_amd64.whl"
    WHEEL_BUILD="$SCRIPT_DIR/.wheel_build_$PYVER"
    rm -rf "$WHEEL_BUILD"
    mkdir -p "$WHEEL_BUILD/$DIST_INFO"

    # Copy extension to wheel root (matches wheel.install-dir = "." in pyproject.toml)
    cp "$PYD_FILE" "$WHEEL_BUILD/"

    # Create METADATA (matches scikit-build-core output)
    cat > "$WHEEL_BUILD/$DIST_INFO/METADATA" << EOF
Metadata-Version: 2.2
Name: cdae_native
Version: ${NATIVE_VERSION}
Summary: C++ DAE XML and CDAE binary parser/writer for Blender addons
Requires-Python: >=3.11
EOF

    # Create WHEEL file with Windows tag
    cat > "$WHEEL_BUILD/$DIST_INFO/WHEEL" << EOF
Wheel-Version: 1.0
Generator: build_windows.sh (cross-compile from Linux)
Root-Is-Purelib: false
Tag: cp${PYVER_NODOT}-cp${PYVER_NODOT}-win_amd64
EOF

    # Create wheel using Python (generates RECORD with correct hashes)
    "$VENV_PY" - "$WHEEL_BUILD" "$WHEELS_DIR/$WHEEL_NAME" "$DIST_INFO" << 'PYEOF'
import hashlib, base64, os, sys, zipfile

wheel_dir, output, dist_info = sys.argv[1], sys.argv[2], sys.argv[3]

# Collect all files
entries = []
for root, dirs, files in os.walk(wheel_dir):
    for f in sorted(files):
        filepath = os.path.join(root, f)
        relpath = os.path.relpath(filepath, wheel_dir).replace(os.sep, '/')
        entries.append((filepath, relpath))

record_path = f"{dist_info}/RECORD"
record_lines = []

with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as zf:
    for filepath, relpath in entries:
        if relpath == record_path:
            continue
        with open(filepath, 'rb') as fh:
            data = fh.read()
        zf.writestr(relpath, data)
        h = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b'=').decode()
        record_lines.append(f"{relpath},sha256={h},{len(data)}")
    # RECORD entry for itself (no hash, no size)
    record_lines.append(f"{record_path},,")
    zf.writestr(record_path, "\n".join(record_lines) + "\n")

print(f"Created: {output}")
PYEOF

    # Cleanup
    rm -rf "$VENV_DIR" "$BUILD_DIR" "$WHEEL_BUILD"
done

# Cleanup temp files
rm -f "$SCRIPT_DIR/.toolchain-win.cmake" "$PBS_API_FILE"

echo ""
echo "============================================"
echo "Cross-compilation complete."
echo "============================================"
echo ""
echo "Windows wheel(s) in $WHEELS_DIR:"
ls -la "$WHEELS_DIR/"cdae_native-*win_amd64*.whl 2>/dev/null || echo "No Windows wheels found."

# Update blender_manifest.toml with built wheel(s) and platform(s)
echo ""
echo "Updating blender_manifest.toml..."
python3 "$SCRIPT_DIR/update_manifest.py"
