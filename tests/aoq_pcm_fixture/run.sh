#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
src="$repo/ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq"
build=$(mktemp -d "${TMPDIR:-/tmp}/aoq-pcm-fixture.XXXXXX")
trap 'rm -rf -- "$build"' EXIT
"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$src" "$here/test_pcm_fixture.cpp" "$src/aoq_pcm_fixture.cpp" -o "$build/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$build/test"
printf '#include "aoq_pcm_fixture.h"\nint main(void){return 0;}\n' > "$build/c-header.c"
"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -I"$src" -c "$build/c-header.c" -o "$build/c-header.o"
python3 "$here/check_coefficients.py"
