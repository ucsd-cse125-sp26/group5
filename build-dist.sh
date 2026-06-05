#!/usr/bin/env bash
#
# build-dist [normal|profiling] [--no-build] [--no-upload] [--keep]
#
# Cross-compiles the Windows client+server, stages the runtime files, zips
# them, and uploads the zip to the CSE 125 team server (port 222) under
# /var/www/html/cse125/2026/cse125g5/builds/.
#
# Server account: $CSE125_USER (or prompts). The ssh key at ./id_cse125 is
# used automatically if present.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_ROOT"

SERVER_HOST="cse125.ucsd.edu"
SERVER_PORT=222
SERVER_DIR="/var/www/html/cse125/2026/cse125g5/builds"
WEB_BASE_URL="https://cse125.ucsd.edu/2026/cse125g5/builds"

variant=""
do_build=1
do_upload=1
keep_stage=0

usage() {
	cat <<EOF
usage: build-dist [normal|profiling] [options]

variants:
  normal      Windows release build (default)
  profiling   Windows build with -DENABLE_PROFILING=ON

options:
  --no-build    skip cmake/ninja, package what is already in the build dir
  --no-upload   build and zip locally, skip scp
  --keep        keep the staging directory after zipping
  -h, --help    show this message
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		normal|profiling) variant="$1"; shift ;;
		--no-build) do_build=0; shift ;;
		--no-upload) do_upload=0; shift ;;
		--keep) keep_stage=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "build-dist: unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

if [[ -z "$variant" ]]; then
	variant="normal"
fi

case "$variant" in
	normal)
		build_dir="build-windows"
		cmake_extra=()
		;;
	profiling)
		build_dir="build-winprof"
		cmake_extra=(-DENABLE_PROFILING=ON)
		;;
esac

if [[ $do_build -eq 1 ]]; then
	echo "==> Configuring $variant Windows build in $build_dir"
	# macOS ships Bash 3.2; "${array[@]}" on an empty array trips `set -u`.
	if [[ ${#cmake_extra[@]} -gt 0 ]]; then
		cmake -B "$build_dir" -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE=toolchains/windows.cmake \
			-DCMAKE_BUILD_TYPE=Release \
			"${cmake_extra[@]}"
	else
		cmake -B "$build_dir" -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE=toolchains/windows.cmake \
			-DCMAKE_BUILD_TYPE=Release
	fi
	echo "==> Building"
	(cd "$build_dir" && ninja)
fi

required=(client.exe server.exe assets shaders)
for item in "${required[@]}"; do
	if [[ ! -e "$build_dir/$item" ]]; then
		echo "build-dist: missing $build_dir/$item — run a full build first" >&2
		exit 1
	fi
done

git_sha="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
date_tag="$(date +%Y%m%d-%H%M%S)"
zip_name="cse125g5-windows-${variant}-${date_tag}-${git_sha}.zip"
stage_dir="$(mktemp -d -t cse125-dist-XXXXXX)"
pkg_root="${stage_dir}/cse125g5-windows-${variant}"

cleanup() {
	if [[ $keep_stage -eq 0 ]]; then
		rm -rf "$stage_dir"
	else
		echo "==> Staging dir kept: $stage_dir"
	fi
}
trap cleanup EXIT

mkdir -p "$pkg_root"

echo "==> Staging files into $pkg_root"
cp "$build_dir/client.exe" "$pkg_root/"
cp "$build_dir/server.exe" "$pkg_root/"
cp -r "$build_dir/assets" "$pkg_root/"
cp -r "$build_dir/shaders" "$pkg_root/"
if [[ -d "$build_dir/maps" ]]; then
	cp -r "$build_dir/maps" "$pkg_root/"
fi
if [[ -f "$build_dir/gltf_inspect.exe" ]]; then
	cp "$build_dir/gltf_inspect.exe" "$pkg_root/"
fi

cat > "$pkg_root/BUILD_INFO.txt" <<EOF
variant: $variant
git: $(git rev-parse HEAD 2>/dev/null || echo unknown)
branch: $(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)
built: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
host: $(uname -a)
EOF

zip_path="${REPO_ROOT}/${zip_name}"
echo "==> Creating $zip_name"
(cd "$stage_dir" && zip -qr "$zip_path" "$(basename "$pkg_root")")
echo "    $(du -h "$zip_path" | cut -f1) $zip_path"

if [[ $do_upload -eq 0 ]]; then
	echo "==> Skipping upload (--no-upload)"
	exit 0
fi

server_user="${CSE125_USER:-}"
if [[ -z "$server_user" ]]; then
	read -r -p "CSE125 server username: " server_user
fi
if [[ -z "$server_user" ]]; then
	echo "build-dist: server username required (set CSE125_USER)" >&2
	exit 1
fi

ssh_args=(-p "$SERVER_PORT")
scp_args=(-P "$SERVER_PORT")
if [[ -f "${REPO_ROOT}/id_cse125" ]]; then
	ssh_args+=(-i "${REPO_ROOT}/id_cse125" -o IdentitiesOnly=yes)
	scp_args+=(-i "${REPO_ROOT}/id_cse125" -o IdentitiesOnly=yes)
fi

echo "==> Ensuring remote dir $SERVER_DIR exists"
ssh "${ssh_args[@]}" "${server_user}@${SERVER_HOST}" "mkdir -p '$SERVER_DIR'"

echo "==> Uploading $zip_name to ${server_user}@${SERVER_HOST}:${SERVER_DIR}"
scp "${scp_args[@]}" "$zip_path" \
	"${server_user}@${SERVER_HOST}:${SERVER_DIR}/${zip_name}"

echo "==> Done: ${WEB_BASE_URL}/${zip_name}"
