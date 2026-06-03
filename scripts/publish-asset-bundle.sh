#!/usr/bin/env bash
#
# publish-asset-bundle.sh [--no-upload] [--keep]
#
# Builds two asset bundles from the local (gitignored) game assets and uploads
# them to the CSE 125 team server under builds/asset-bundles/:
#
#   full-<date>-<sha>.zip     assets/ maps/assets/ shaders/  (client installs + fetch-assets)
#   server-<date>-<sha>.zip   maps/assets/                   (server container startup fetch)
#
# Stable full-latest.zip / server-latest.zip aliases are updated atomically on
# the server (copy + rename), so the container and clients always pull the newest.
#
# Bundles are FLAT (contents at the zip root, no wrapper folder) so extracting in
# place lands assets/, maps/, shaders/ right next to the binary, matching the
# server's exeDir()-relative lookups.
#
# Server account: $CSE125_USER (or prompts). The ssh key at ./id_cse125 is used
# automatically if present. Requires `zip` and a full local asset checkout.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SERVER_HOST="cse125.ucsd.edu"
SERVER_PORT=222
SERVER_DIR="/var/www/html/cse125/2026/cse125g5/builds/asset-bundles"
WEB_BASE_URL="https://cse125.ucsd.edu/2026/cse125g5/builds/asset-bundles"

SHADERS_SRC="src/client/shaders"

do_upload=1
keep_stage=0

usage() {
	cat <<EOF
usage: publish-asset-bundle.sh [options]

Builds full + slim asset bundles and uploads them to
  ${SERVER_HOST}:${SERVER_DIR}

options:
  --no-upload   build the zips locally, skip scp
  --keep        keep the staging directory after zipping
  -h, --help    show this message
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--no-upload) do_upload=0; shift ;;
		--keep) keep_stage=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "publish-asset-bundle: unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

command -v zip >/dev/null || { echo "publish-asset-bundle: zip not found" >&2; exit 1; }

required=(assets maps/assets "$SHADERS_SRC")
for item in "${required[@]}"; do
	if [[ ! -e "$item" ]]; then
		echo "publish-asset-bundle: missing $item — need a full local asset checkout" >&2
		exit 1
	fi
done

git_sha="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
date_tag="$(date +%Y%m%d)"
full_name="full-${date_tag}-${git_sha}.zip"
slim_name="server-${date_tag}-${git_sha}.zip"

stage_dir="$(mktemp -d -t cse125-bundle-XXXXXX)"
cleanup() {
	if [[ $keep_stage -eq 0 ]]; then
		rm -rf "$stage_dir"
	else
		echo "==> Staging dir kept: $stage_dir"
	fi
}
trap cleanup EXIT

# ---- FULL bundle: assets/ maps/assets/ shaders/ at zip root ----
full_stage="$stage_dir/full"
mkdir -p "$full_stage/maps"
cp -r assets "$full_stage/assets"
cp -r maps/assets "$full_stage/maps/assets"
cp -r "$SHADERS_SRC" "$full_stage/shaders"

# ---- SLIM bundle: maps/assets/ at zip root ----
slim_stage="$stage_dir/slim"
mkdir -p "$slim_stage/maps"
cp -r maps/assets "$slim_stage/maps/assets"

full_path="${REPO_ROOT}/${full_name}"
slim_path="${REPO_ROOT}/${slim_name}"

echo "==> Creating $full_name (assets/ maps/ shaders/)"
(cd "$full_stage" && zip -qr "$full_path" .)
echo "    $(du -h "$full_path" | cut -f1) $full_path"

echo "==> Creating $slim_name (maps/)"
(cd "$slim_stage" && zip -qr "$slim_path" .)
echo "    $(du -h "$slim_path" | cut -f1) $slim_path"

if [[ $do_upload -eq 0 ]]; then
	echo "==> Skipping upload (--no-upload)"
	exit 0
fi

server_user="${CSE125_USER:-}"
if [[ -z "$server_user" ]]; then
	read -r -p "CSE125 server username: " server_user
fi
if [[ -z "$server_user" ]]; then
	echo "publish-asset-bundle: server username required (set CSE125_USER)" >&2
	exit 1
fi

ssh_args=(-p "$SERVER_PORT")
scp_args=(-P "$SERVER_PORT")
if [[ -f "${REPO_ROOT}/id_cse125" ]]; then
	ssh_args+=(-i "${REPO_ROOT}/id_cse125" -o IdentitiesOnly=yes)
	scp_args+=(-i "${REPO_ROOT}/id_cse125" -o IdentitiesOnly=yes)
fi

remote="${server_user}@${SERVER_HOST}"

echo "==> Ensuring remote dir $SERVER_DIR exists"
ssh "${ssh_args[@]}" "$remote" "mkdir -p '$SERVER_DIR'"

echo "==> Uploading $full_name and $slim_name"
scp "${scp_args[@]}" "$full_path" "$slim_path" "$remote:$SERVER_DIR/"

# Update the stable -latest aliases atomically on the server: copy the freshly
# uploaded versioned zip, then rename over the alias on the same filesystem so a
# concurrent fetch never sees a half-written file. Avoids re-uploading ~600MB.
echo "==> Updating full-latest.zip / server-latest.zip"
ssh "${ssh_args[@]}" "$remote" "
	set -e
	cd '$SERVER_DIR'
	cp -f '$full_name' full-latest.zip.tmp && mv -f full-latest.zip.tmp full-latest.zip
	cp -f '$slim_name' server-latest.zip.tmp && mv -f server-latest.zip.tmp server-latest.zip
"

echo "==> Done"
echo "    FULL: ${WEB_BASE_URL}/full-latest.zip   (and ${full_name})"
echo "    SLIM: ${WEB_BASE_URL}/server-latest.zip (and ${slim_name})"
