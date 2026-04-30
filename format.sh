#!/usr/bin/env bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i

if [[ "$(uname)" == "Darwin" ]]; then
  EXTRA="--extra-arg=-isysroot --extra-arg=$(xcrun --show-sdk-path)"
else
  EXTRA=""
fi

find src -name '*.cpp' | xargs clang-tidy --fix -p build \
  $EXTRA \
  --header-filter='.*' \
  --exclude-header-filter='(lib|build)/.*'

