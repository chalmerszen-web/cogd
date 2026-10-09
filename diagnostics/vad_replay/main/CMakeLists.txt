get_filename_component(agent_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(sr "${agent_root}/third_party/esp-sr")
set(vendor "${sr}/lib/esp32c3/libesp_audio_processor.a")
file(SHA256 "${vendor}" vendor_sha)
idf_component_register(SRCS "replay.c" "${agent_root}/plugins/audio/voice.c"
  "${agent_root}/plugins/audio/tonal.c" "${agent_root}/core/crc.c"
  INCLUDE_DIRS "${agent_root}/core" "${agent_root}/plugins/audio" "${sr}/include/esp32c3"
  REQUIRES esp_driver_usb_serial_jtag esp_timer heap)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror)
target_compile_definitions(${COMPONENT_LIB} PRIVATE asm=__asm__ VENDOR_SHA="${vendor_sha}")
if(REPLAY_NS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE REPLAY_NS=1)
endif()
add_prebuilt_library(replay_vendor "${vendor}")
target_link_libraries(${COMPONENT_LIB} INTERFACE replay_vendor)
