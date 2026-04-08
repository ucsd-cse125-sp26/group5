#!/usr/bin/env bash

set -e

cmake -B build -G Ninja 
(
	cd build || exit
	ninja
)
