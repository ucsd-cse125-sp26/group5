#!/usr/bin/env bash

set -e

cmake -B build-linux-gcc -G Ninja -DCMAKE_TOOLCHAIN_FILE=toolchains/linux-gcc.cmake
(
	cd build-linux-gcc || exit
	ninja
)
