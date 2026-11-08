#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
src="$repo/ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq"
build=$(mktemp -d "${TMPDIR:-/tmp}/aoq-tts-play.XXXXXX")
trap 'rm -rf -- "$build"' EXIT
"${CC:-gcc}" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$here/stubs" -I"$src" "$here/test_tts_play.c" "$src/aoq_tts_play.c" -o "$build/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$build/test"
