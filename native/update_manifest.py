#!/usr/bin/env python3
"""Update blender_manifest.toml platforms + wheels based on actual .whl files.

Scans ../io_beamng_dae/wheels/ for cdae_native-*.whl, maps platform tags to Blender
platform names, and rewrites the platforms + wheels sections of
blender_manifest.toml.

Called automatically by build.sh and build_windows.sh after a successful build.
Can also be run standalone: python3 update_manifest.py
"""

import re
import sys
from pathlib import Path

# Supported Blender extension Python ABI versions.
# Only wheels matching these versions are included in the manifest.
# Blender 5.0.1 → 3.11, Blender 5.2+ → 3.13.
# Do NOT build or include 3.14 wheels — no Blender version uses 3.14 yet.
SUPPORTED_PY_VERSIONS = {"3.11", "3.13"}

# Wheel platform tag -> Blender platform name
# macOS mappings kept for community use; BeamNG.drive has no macOS binaries,
# so no macOS wheels are built by default. Build them yourself and update_manifest.py
# will pick them up automatically.
PLATFORM_MAP = {
    "linux_x86_64": "linux-x64",
    "win_amd64": "windows-x64",
    "macosx_11_0_arm64": "macos-arm64",
    "macosx_10_9_x86_64": "macos-x64",
}

SCRIPT_DIR = Path(__file__).resolve().parent
ADDON_DIR = SCRIPT_DIR.parent / "io_beamng_dae"
MANIFEST = ADDON_DIR / "blender_manifest.toml"
WHEELS_DIR = ADDON_DIR / "wheels"


def map_platform(wheel_filename: str) -> str | None:
    """Extract Blender platform name from a wheel filename."""
    for tag, platform in PLATFORM_MAP.items():
        if tag in wheel_filename:
            return platform
    return None


def main() -> int:
    if not MANIFEST.exists():
        print(f"Error: {MANIFEST} not found", file=sys.stderr)
        return 1

    # Scan for actual wheels (filter to supported Python versions only)
    all_wheels = sorted(WHEELS_DIR.glob("cdae_native-*.whl"))
    wheels = []
    for whl in all_wheels:
        # Extract Python version tag from filename (e.g. cp311, cp313, cp314)
        m = re.search(r'-cp(\d)(\d+)-', whl.name)
        if m:
            py_ver = f"{m.group(1)}.{m.group(2)}"
            if py_ver not in SUPPORTED_PY_VERSIONS:
                continue
        wheels.append(whl)
    if not wheels:
        print("No cdae_native wheels found in ../io_beamng_dae/wheels/")
        return 0

    # Build the wheels list entries and collect platforms
    wheel_entries = []
    platforms = set()
    for whl in wheels:
        rel = f"./wheels/{whl.name}"
        wheel_entries.append(rel)
        platform = map_platform(whl.name)
        if platform:
            platforms.add(platform)

    # Read manifest
    content = MANIFEST.read_text()

    # Replace platforms line
    platforms_sorted = sorted(platforms)
    platforms_str = ", ".join(f'"{p}"' for p in platforms_sorted)
    content = re.sub(
        r'^platforms\s*=\s*\[.*\]',
        f'platforms = [{platforms_str}]',
        content,
        count=1,
        flags=re.MULTILINE,
    )

    # Replace wheels block (everything between 'wheels = [' and the closing ']')
    wheel_lines = []
    for entry in wheel_entries:
        wheel_lines.append(f'  "{entry}",')
    wheels_block = "wheels = [\n" + "\n".join(wheel_lines) + "\n]"
    content = re.sub(
        r'^wheels\s*=\s*\[.*?\]',
        wheels_block,
        content,
        count=1,
        flags=re.MULTILINE | re.DOTALL,
    )

    MANIFEST.write_text(content)
    print(f"Updated {MANIFEST.name}:")
    print(f"  platforms = {platforms_sorted}")
    print(f"  wheels = {len(wheel_entries)} entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
