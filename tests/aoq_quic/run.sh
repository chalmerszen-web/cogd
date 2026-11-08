#!/usr/bin/env bash
# Linux/WSL host tests. No ESP-IDF, credentials, network or device required.
set -euo pipefail
test_dir=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$test_dir/../.." && pwd)
src="$repo/ESP32-P4-WIFI6-Touch-LCD-3.5/components/esp_http3"
cxx=${CXX:-g++}
build=$(mktemp -d "${TMPDIR:-/tmp}/cogd-aoq-quic.XXXXXX")
trap 'rm -rf -- "$build"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}halt_on_error=1"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1"

common=(-std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
includes=(-I"$src/include" -I"$src/tests/host/stubs")
quic=("$src/src/quic/quic_frame.cc" "$src/src/quic/quic_varint.cc" "$src/src/quic/quic_types.cc")

"$cxx" "${common[@]}" "${includes[@]}" \
    "$test_dir/transport_parameter_regression.cc" "${quic[@]}" \
    -o "$build/transport_parameter_regression"
"$build/transport_parameter_regression"

python3 "$test_dir/extract_methods.py" "$src/src/core/quic_connection.cc" "$build/raw_tls_methods.inc"
"$cxx" "${common[@]}" -ffunction-sections -fdata-sections "${includes[@]}" -I"$build" \
    "$test_dir/raw_tls_regression.cc" "$src/src/tls/tls_handshake.cc" \
    "$src/src/core/flow_controller.cc" "${quic[@]}" \
    -Wl,--gc-sections -lcrypto -o "$build/raw_tls_regression"
"$build/raw_tls_regression"
python3 "$test_dir/extract_methods.py" "$src/src/core/quic_connection.cc" "$build/cid_methods.inc" cid
"$cxx" "${common[@]}" "${includes[@]}" -I"$build" \
    "$test_dir/cid_regression.cc" "${quic[@]}" \
    -o "$build/cid_regression"
"$build/cid_regression"
python3 "$test_dir/extract_methods.py" "$src/src/core/quic_connection.cc" "$build/datagram_methods.inc" datagram
"$cxx" "${common[@]}" "${includes[@]}" -I"$build" \
    "$test_dir/datagram_regression.cc" "${quic[@]}" -o "$build/datagram_regression"
"$build/datagram_regression"
python3 "$test_dir/extract_methods.py" "$src/src/core/quic_connection.cc" "$build/ack_methods.inc" ack
"$cxx" "${common[@]}" "${includes[@]}" -I"$build" \
    "$test_dir/ack_credit_regression.cc" "$src/src/core/ack_manager.cc" "$src/src/core/loss_detector.cc" \
    "${quic[@]}" -o "$build/ack_credit_regression"
"$build/ack_credit_regression"
"$cxx" "${common[@]}" -ffunction-sections -fdata-sections "${includes[@]}" \
    "$test_dir/ack_timer_regression.cc" "$src/src/core/ack_manager.cc" "${quic[@]}" \
    -Wl,--gc-sections -o "$build/ack_timer_regression"
"$build/ack_timer_regression"
printf '%s\n' 'AOQ QUIC host regressions PASS (transport, raw, ALPN, authentication policy)'
