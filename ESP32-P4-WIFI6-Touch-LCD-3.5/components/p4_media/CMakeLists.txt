file(GLOB h264_common "openh264/codec/common/src/*.cpp")
file(GLOB h264_decoder "openh264/codec/decoder/core/src/*.cpp" "openh264/codec/decoder/plus/src/*.cpp")
idf_component_register(SRCS ${h264_common} ${h264_decoder} "p4_media.cpp"
  INCLUDE_DIRS "openh264/codec/api" "."
  PRIV_INCLUDE_DIRS "openh264/codec/api/wels" "openh264/codec/common/inc" "openh264/codec/decoder/core/inc" "openh264/codec/decoder/plus/inc"
  REQUIRES pthread esp_timer heap esp_http_client esp-tls esp_partition esp_driver_jpeg espressif__esp_audio_codec)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -fno-strict-aliasing -O2)
target_compile_definitions(${COMPONENT_LIB} PRIVATE WELS_DECODER)
