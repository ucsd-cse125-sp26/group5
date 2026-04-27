#!/usr/bin/env bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src -name '*.cpp' | xargs clang-tidy --fix -p build --header-filter='.*' --exclude-header-filter='(lib|build)/.*'
