#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd "$(dirname "$0")" && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/cogd-aoq-camera.XXXXXX")
trap 'rm -rf -- "$build"' EXIT
${CXX:-g++} -std=c++17 -g -O1 -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -I"$test_dir/stubs" -I"$test_dir/../aoq_session/stubs" \
  "$test_dir/test_camera.cpp" -o "$build/camera"
"$build/camera"
