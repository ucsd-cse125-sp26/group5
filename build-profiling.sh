
#!/bin/bash
set -e

cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DENABLE_PROFILING=ON "$@"
(
	cd build || exit
	ninja
)