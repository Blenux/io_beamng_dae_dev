#!/bin/bash
# Package io_beamng_dae as zip for Blender Extensions upload

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ADDON_DIR="$ROOT_DIR/io_beamng_dae"

# Extract version from blender_manifest.toml for the zip filename
ADDON_VERSION=$(grep -oP '^version\s*=\s*"\K[^"]+' "$ADDON_DIR/blender_manifest.toml")
if [ -z "$ADDON_VERSION" ]; then
    echo "Error: Could not extract version from blender_manifest.toml"
    exit 1
fi

ZIP_NAME="io_beamng_dae_v${ADDON_VERSION}.zip"
OUTPUT_PATH="$ROOT_DIR/$ZIP_NAME"

ADDON_ID=$(grep -oP '^id\s*=\s*"\K[^"]+' "$ADDON_DIR/blender_manifest.toml")

# Stage io_beamng_dae/ into a temp dir named after the extension id, so the zip
# has the correct top-level folder name (Blender uses the manifest id,
# but a matching folder name helps users who install from disk)
STAGING=$(mktemp -d)
cleanup() { rm -rf "$STAGING"; }
trap cleanup EXIT

mkdir -p "$STAGING/$ADDON_ID"
rsync -a \
  --exclude '__pycache__' \
  --exclude '*.pyc' \
  "$ADDON_DIR/" "$STAGING/$ADDON_ID/"

cd "$STAGING"
zip -r "$OUTPUT_PATH" "$ADDON_ID/"

echo "Created: $OUTPUT_PATH"
echo "Contents:"
unzip -l "$OUTPUT_PATH" | tail -n +4 | head -n -2
echo ""
echo "Size: $(du -h "$OUTPUT_PATH" | cut -f1)"
