#!/usr/bin/env bash

set -e

cmake -B build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=toolchains/windows.cmake
(
	cd build-windows || exit
	ninja
)
