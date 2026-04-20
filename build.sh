#!/usr/bin/env bash

set -e

cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=1 "$@"
(
	cd build || exit
	ninja
)
