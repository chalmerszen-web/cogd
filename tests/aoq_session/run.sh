#!/usr/bin/env bash
# Linux/WSL; no ESP-IDF installation, account, device, or network is used.
set -euo pipefail
test_dir=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$test_dir/../.." && pwd)
src="$repo/ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq"
cc=${CC:-gcc}
cxx=${CXX:-g++}
build=$(mktemp -d "${TMPDIR:-/tmp}/cogd-aoq-session.XXXXXX")
trap 'rm -rf -- "$build"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}halt_on_error=1"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1"
flags=(-g -O1 -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror)
includes=(-I"$test_dir/stubs" -I"$repo/third_party/cjson")
"$cc" -std=c11 "${flags[@]}" "${includes[@]}" \
    -c "$repo/third_party/cjson/cJSON.c" -o "$build/cJSON.o"
# Production Session is included in this one translation unit so the tests can
# inject fragmented/duplicate frames without opening a real socket or token API.
"$cxx" -std=c++17 "${flags[@]}" -pthread -Wno-subobject-linkage "${includes[@]}" \
    "$test_dir/test_session.cpp" "$src/aoq_wire.cpp" "$src/aoq_objects.cpp" \
    "$src/aoq_media_control.cpp" "$src/aoq_video.cpp" "$build/cJSON.o" -o "$build/session"
"$cxx" -std=c++17 "${flags[@]}" "${includes[@]}" \
    "$test_dir/test_tts.cpp" "$src/aoq_objects.cpp" "$build/cJSON.o" -o "$build/tts"
"$cxx" -std=c++17 "${flags[@]}" "${includes[@]}" \
    "$test_dir/test_asr.cpp" "$src/aoq_pcm_fixture.cpp" "$build/cJSON.o" -o "$build/asr"
"$cxx" -std=c++17 "${flags[@]}" "${includes[@]}" \
    "$test_dir/test_dialogue.cpp" "$src/aoq_pcm_fixture.cpp" "$src/aoq_objects.cpp" "$build/cJSON.o" -o "$build/dialogue"
"$cxx" -std=c++17 "${flags[@]}" -Wno-subobject-linkage "${includes[@]}" \
    "$test_dir/test_conversation.cpp" "$src/aoq_objects.cpp" "$build/cJSON.o" -o "$build/conversation"
"$cxx" -std=c++17 "${flags[@]}" -Wno-subobject-linkage "${includes[@]}" \
    "$test_dir/test_speech.cpp" "$src/aoq_objects.cpp" "$build/cJSON.o" -o "$build/speech"
"$cxx" -std=c++17 "${flags[@]}" -pthread "${includes[@]}" \
    "$test_dir/test_prefetch.cpp" -o "$build/prefetch"
"$build/session"
"$build/tts"
"$build/asr"
"$build/dialogue"
"$build/conversation"
"$build/speech"
"$build/prefetch"
