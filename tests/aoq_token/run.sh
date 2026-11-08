#!/usr/bin/env bash
# Linux/WSL; only the checked-in synthetic fixture and formal source are read.
set -euo pipefail
test_dir=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$test_dir/../.." && pwd)
cc=${CC:-gcc}
cxx=${CXX:-g++}
build=$(mktemp -d "${TMPDIR:-/tmp}/cogd-aoq-token.XXXXXX")
trap 'rm -rf -- "$build"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}halt_on_error=1"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1"
common=(-g -O1 -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
"$cc" -std=c11 "${common[@]}" -I"$repo/third_party/cjson" \
    -c "$repo/third_party/cjson/cJSON.c" -o "$build/cJSON.o"
# Deliberately tests an out-of-range C ABI enum; keep all other UBSan checks.
"$cxx" -std=c++17 "${common[@]}" -fno-sanitize=enum \
    -I"$test_dir/stubs" -I"$repo/third_party/cjson" \
    "$test_dir/test_token.cpp" "$build/cJSON.o" -o "$build/test_token"
"$build/test_token"
