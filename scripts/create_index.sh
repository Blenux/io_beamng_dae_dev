#!/bin/bash
# Generate index.json for a Blender Extensions static repository.
#
# Usage:
#   ./create_index.sh [--blender PATH] <repo-dir>
#
# Arguments:
#   <repo-dir>           Directory containing .zip extension packages.
#
# Options:
#   --blender PATH       Path to the Blender executable (default: auto-detect).
#   -h, --help           Show this help message.
#
# Examples:
#   ./create_index.sh /path/to/packages
#   ./create_index.sh --blender /opt/blender/blender /path/to/packages
#
# Reference:
#   https://docs.blender.org/manual/en/latest/advanced/extensions/creating_repository/static_repository.html

set -euo pipefail

# --- Defaults ---------------------------------------------------------------

# Try to find Blender automatically if not overridden by --blender
DEFAULT_BLENDER=""
for candidate in \
    "/home/juso3d/.local/share/Steam/steamapps/common/Blender/blender" \
    "$(command -v blender 2>/dev/null || true)"; do
    if [ -x "$candidate" ]; then
        DEFAULT_BLENDER="$candidate"
        break
    fi
done

BLENDER="$DEFAULT_BLENDER"
REPO_DIR=""

# --- Parse arguments --------------------------------------------------------

while [ $# -gt 0 ]; do
    case "$1" in
        --blender)
            BLENDER="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        --*)
            echo "Error: Unknown option '$1'"
            exit 1
            ;;
        *)
            if [ -z "$REPO_DIR" ]; then
                REPO_DIR="$1"
            else
                echo "Error: Unexpected argument '$1' (repo-dir already set to '$REPO_DIR')"
                exit 1
            fi
            shift
            ;;
    esac
done

# --- Validate ---------------------------------------------------------------

if [ -z "$REPO_DIR" ]; then
    echo "Error: Missing required <repo-dir> argument."
    echo "Usage: $0 [--blender PATH] <repo-dir>"
    exit 1
fi

if [ ! -d "$REPO_DIR" ]; then
    echo "Error: Directory does not exist: $REPO_DIR"
    exit 1
fi

if [ -z "$BLENDER" ] || [ ! -x "$BLENDER" ]; then
    echo "Error: Blender executable not found."
    echo "Specify it with --blender /path/to/blender"
    exit 1
fi

# Check that at least one .zip exists in the repo directory
zip_count=$(find "$REPO_DIR" -maxdepth 1 -name '*.zip' | wc -l)
if [ "$zip_count" -eq 0 ]; then
    echo "Warning: No .zip files found in $REPO_DIR"
fi

# --- Run --------------------------------------------------------------------

echo "Generating index.json in: $REPO_DIR"
"$BLENDER" --command extension server-generate --repo-dir="$REPO_DIR"
echo "Done: $REPO_DIR/index.json"