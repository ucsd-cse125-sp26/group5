#!/usr/bin/env bash
# Usage: scripts/blend2glb.sh <map_name>

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <map_name>" >&2
  exit 64
fi

NAME="$1"
if [[ ! "$NAME" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$ ]]; then
  echo "invalid map name: $NAME" >&2
  exit 64
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO_ROOT/maps/$NAME.blend"
DST="$REPO_ROOT/maps/assets/$NAME.glb"
BLENDER_BIN="${BLENDER:-blender}"

if [[ ! -f "$SRC" ]]; then
  echo "missing input: $SRC (check out the map first)" >&2
  exit 1
fi
if ! command -v "$BLENDER_BIN" >/dev/null 2>&1; then
  echo "blender binary not found (set \$BLENDER or install blender)" >&2
  exit 1
fi

mkdir -p "$(dirname "$DST")"

"$BLENDER_BIN" --background "$SRC" --python-expr "
import bpy
bpy.ops.export_scene.gltf(
    filepath=r'''$DST''',
    export_format='GLB',
    export_yup=False,
    export_lights=True,
)
"

echo "exported $SRC -> $DST"
