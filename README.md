# BeamNG DAE/CDAE Import/Export

A Blender extension for importing and exporting BeamNG.drive Collada (`.dae`) and Cached Collada (`.cdae`) mesh files. Uses a C++ native module for fast parsing and writing, with numpy-vectorized Blender mesh operations.

## Features

- Import `.dae` (Collada XML) and `.cdae` (Cached Collada binary) files
- Export meshes to `.dae` or `.cdae` format
- Preserves vertices, normals, UVs (UV0/UV1), vertex colors, materials, sharp edges/faces, and node hierarchy transforms
- PBR material support (base color, roughness, metallic) via Principled BSDF
- Zstd-compressed CDAE binary format
- Drag-and-drop file import via Blender FileHandler
- Vectorized with numpy `foreach_get`/`foreach_set` for performance

## Requirements

- Blender 5.0.1 or later
- Python 3.11 or 3.13 (matching Blender's bundled Python ABI)
- Supported platforms: **Linux x86_64**, **Windows x86_64**

## Installation

1. Download or build the extension (see [Building](#building) below)
2. In Blender, go to **Edit > Preferences > Extensions**
3. Install from disk, selecting the `.zip` package or the extension folder

Pre-built wheels for Linux and Windows (cp311 and cp313) are included in `wheels/`.

### Extension Repository (Recommended)

You can add this repository directly in Blender for automatic install and updates:

1. In Blender, go to **Edit > Preferences > Extensions**
2. Click **Repositories > [+] > Add Remote Repository**
3. Paste the repository URL:
   ```
   https://blenux.github.io/blender-extensions/index.json
   ```
4. The extension will appear in the list — click **Install** and **Update** as needed

### Manual Install

Download the `.zip` from [GitHub Releases](https://github.com/Blenux/io_beamng_dae_dev/releases) and use **Install from Disk**.

## Usage

### Import

1. **File > Import > BeamNG Collada (.dae, .cdae)**
2. Select a `.dae` or `.cdae` file
3. Adjust scale and custom normals options if needed
4. Meshes are imported into a collection named after the file

### Export

1. **File > Export > BeamNG Collada (.dae, .cdae)**
2. Choose format: DAE (XML) or CDAE (binary)
3. Toggle selection-only, modifiers, custom normals, UV layers, scale
4. Export writes the file via the C++ native module

Drag-and-drop is also supported — drop `.dae`/`.cdae` files directly into Blender's viewport.

## Project Structure

```
io_beamng_dae_dev/
├── io_beamng_dae/                   # Blender extension (shipped as-is)
│   ├── __init__.py          # Addon registration
│   ├── blender_manifest.toml# Blender extension manifest
│   ├── operators/
│   │   ├── import_dae.py    # Import operator
│   │   └── export_dae.py    # Export operator
│   ├── core/
│   │   ├── mesh_builder.py  # Build Blender meshes from parsed data (import)
│   │   └── mesh_extractor.py# Extract Blender mesh data for export
│   ├── ui/
│   │   ├── import_panel.py  # Import options panel
│   │   ├── export_panel.py  # Export options panel
│   │   └── filehandler.py   # Drag-and-drop file handler
│   └── wheels/              # Pre-built platform wheels
├── native/                  # C++ native module (cdae_native) — not shipped
│   ├── src/                 # C++ source (parser, writer, msgpack)
│   ├── CMakeLists.txt       # CMake build config
│   ├── pyproject.toml       # Python build config (scikit-build-core)
│   ├── build.sh             # Linux build script
│   ├── build.bat            # Windows build script (native)
│   ├── build_windows.sh     # Windows cross-compile from Linux (MinGW)
│   ├── build_macos.sh       # macOS build script (community)
│   └── update_manifest.py   # Auto-update manifest platforms + wheels
├── scripts/                 # Repo-level tooling
│   ├── package_zip.sh       # Package io_beamng_dae as zip
│   └── create_index.sh      # Generate index.json for Blender repository
├── tests/                   # Test suite
│   ├── conftest.py          # Pytest fixtures
│   └── test_roundtrip.py    # Import/export roundtrip tests
```

## Building

The native C++ module (`cdae_native`) must be built for your platform and Python ABI version. Build scripts are provided for each platform.

### Prerequisites (all platforms)

- **[uv](https://docs.astral.sh/uv/getting-started/installation/)** — Python version manager
- **CMake** (installed automatically by uv in the build venv)
- **Git** (for FetchContent dependencies: pugixml, zstd)

### Linux

```bash
cd native/

# Build with system Python
./build.sh

# Build matching Blender's Python ABI version
./build.sh --blender-python

# Manual version override
BLENDER_PY_VER=3.13 ./build.sh --blender-python
```

### Windows

**Native build (on Windows):**

```bash
cd native/
build.bat
build.bat 3.13
```

Requires: Visual Studio Build Tools (C++ workload), Python 3.11/3.13 installed.

**Cross-compile from Linux (MinGW-w64):**

```bash
cd native/
./build_windows.sh
./build_windows.sh 3.11 3.13
```

Requires: `mingw-w64` cross-compiler, `cmake`, `curl`, `tar`, `git`, `uv`.

### macOS (Community)

BeamNG.drive has no macOS binaries, so macOS wheels are not built or shipped by default. The build infrastructure is ready for anyone who wants them.

**Prerequisites:**

- **Xcode Command Line Tools:** `xcode-select --install`
- **uv:** [Install instructions](https://docs.astral.sh/uv/getting-started/installation/)
- **Blender** (optional, only if using `--blender-python`)

```bash
cd native/

# Build for current architecture (arm64 on Apple Silicon, x86_64 on Intel)
./build_macos.sh

# Build for a specific architecture
./build_macos.sh --arch arm64
./build_macos.sh --arch x86_64

# Build universal2 wheel (both architectures)
./build_macos.sh --arch universal2

# Build matching Blender's Python ABI version
./build_macos.sh --blender-python
BLENDER_PY_VER=3.13 ./build_macos.sh --blender-python --arch arm64
```

Wheels are output to `../io_beamng_dae/wheels/` and `blender_manifest.toml` is auto-updated by `update_manifest.py`.

## Native Build Notes

### zstd target name detection

`zstd` can export under different target names depending on whether it's found via `find_package` or built via `FetchContent`. `CMakeLists.txt` detects the available target before linking.

### Windows zstd resource file fix

The bundled `zstd` CMake build tries to compile a resource file for the shared DLL (`libzstd-dll.rc`) and fails to locate `zstd.h`. Fixed by disabling everything except the static library before `FetchContent_MakeAvailable(zstd)`:

```cmake
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
```

### macOS CMake additions

`CMakeLists.txt` sets `CMAKE_OSX_DEPLOYMENT_TARGET` (11.0 for arm64, 10.9 for x86_64) and `CMAKE_OSX_ARCHITECTURES` for correct wheel platform tags.

## Releasing

This repo hosts the source code. The extension release (`.zip` + `index.json`) is published to a separate [blender-extensions](https://github.com/Blenux/blender-extensions) repository.

### Manual release workflow

```bash
# 1. Build the extension zip (outputs to repo root)
./scripts/package_zip.sh

# 2. Copy the zip to your blender-extensions repo
cp io_beamng_dae_v*.zip /path/to/blender-extensions/

# 3. Generate index.json using Blender's server-generate command
./scripts/create_index.sh /path/to/blender-extensions/

# 4. Commit and push the blender-extensions repo
```

The `index.json` is generated by Blender's `extension server-generate` command, which includes `archive_url`, `archive_size`, and `archive_hash` for each extension package.

## License

GPL-2.0-or-later
