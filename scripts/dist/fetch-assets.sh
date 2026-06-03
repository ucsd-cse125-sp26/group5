#!/usr/bin/env bash
#
# Downloads the full game asset bundle from the CSE 125 server and extracts it
# next to this script, so assets/, maps/, and shaders/ land beside client/server.
# Ship this inside a binary-only release zip; run it once after unzipping.
#
# Override the source with: ASSET_BUNDLE_URL=... ./fetch-assets.sh

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
URL="${ASSET_BUNDLE_URL:-https://cse125.ucsd.edu/2026/cse125g5/builds/asset-bundles/full-latest.zip}"
ZIP="$DIR/full-latest.zip"

command -v curl >/dev/null || { echo "fetch-assets: curl not found" >&2; exit 1; }
command -v unzip >/dev/null || { echo "fetch-assets: unzip not found (install it, e.g. 'apt install unzip')" >&2; exit 1; }

echo "Downloading $URL"
curl -fL --retry 3 --retry-delay 2 -o "$ZIP" "$URL"

echo "Extracting into $DIR"
unzip -oq "$ZIP" -d "$DIR"
rm -f "$ZIP"

echo "Done. assets/ maps/ shaders/ are next to the binaries."
