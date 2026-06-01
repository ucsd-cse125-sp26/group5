#!/usr/bin/env bash
set -euo pipefail

if command -v clang-format >/dev/null 2>&1; then
  CLANG_FORMAT=clang-format
elif [[ -x /opt/homebrew/opt/llvm/bin/clang-format ]]; then
  CLANG_FORMAT=/opt/homebrew/opt/llvm/bin/clang-format
else
  echo "clang-format not found. Install LLVM or add clang-format to PATH."
  exit 1
fi

if command -v clang-tidy >/dev/null 2>&1; then
  CLANG_TIDY=clang-tidy
elif [[ -x /opt/homebrew/opt/llvm/bin/clang-tidy ]]; then
  CLANG_TIDY=/opt/homebrew/opt/llvm/bin/clang-tidy
else
  echo "clang-tidy not found. Install LLVM or add clang-tidy to PATH."
  exit 1
fi

find src -name '*.cpp' -o -name '*.h' | xargs "$CLANG_FORMAT" -i

if [[ "$(uname)" == "Darwin" ]]; then
  EXTRA="--extra-arg=-isysroot --extra-arg=$(xcrun --show-sdk-path)"
else
  EXTRA=""
fi

find src -name '*.cpp' | xargs "$CLANG_TIDY" --fix -p build \
  $EXTRA \
  --header-filter='.*' \
  --exclude-header-filter='(lib|build)/.*'
