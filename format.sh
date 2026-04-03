#!/usr/bin/env bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src -name '*.cpp' -o -name '*.h' | xargs clang-tidy -p build
