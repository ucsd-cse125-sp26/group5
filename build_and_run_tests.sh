cmake -B build -G Ninja
cd build
ctest --output-on-failure
