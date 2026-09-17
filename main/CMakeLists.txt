option(AGENT_MIC_OVERSAMPLE "32-kHz ADC with anti-aliasing to 16 kHz" ON)
option(AGENT_PACKED_CLIP "Lossless version3 clip storage" ON)
set(AGENT_MIC_ATTEN_DB "12" CACHE STRING "Microphone ADC attenuation")
set(audio_sources)
if(CONFIG_AGENT_AUDIO)
  set(audio_sources
    "../plugins/audio/audio.c" "../plugins/audio/clip.c" "../plugins/audio/voice.c"
    "../plugins/audio/noise.c" "../boards/esp_hi/audio_board.c"
    "../plugins/audio/song.c" "../plugins/audio/song_synth.c" "../plugins/audio/endpoint.c"
    "../plugins/audio/source_stream.c" "../plugins/audio/source_bound.c"
    "../plugins/audio/confirmation.c" "../plugins/audio/confirmation_tail.c"
    "../plugins/audio/tonal.c" "../boards/esp_hi/phase.c" "../boards/esp_hi/phase_clock.c"
    "../core/upload.c")
  if(AGENT_MIC_OVERSAMPLE)
    list(APPEND audio_sources "../plugins/audio/decimate.c")
  endif()
  if(AGENT_BACKGROUND_VERIFY)
    list(APPEND audio_sources "../boards/esp_hi/vad_worker.c")
  endif()
  if(AGENT_PACKED_CLIP)
    list(APPEND audio_sources "../plugins/audio/clip_packed.c")
  endif()
endif()
idf_component_register(
  SRCS "app_main.c" "../core/agent.c" "../core/json.c" "../core/crc.c" "../plugins/llm/engine.c"
       "../plugins/control/resources.c" "../plugins/control/plan.c" "../plugins/control/control.c" "../plugins/control/devices.c" "../boards/esp_hi/control_board.c"
       "../third_party/cjson/cJSON.c" "../platform/espidf/runtime.c" "../platform/espidf/http.c"
       "../plugins/llm/deepseek.c" "../plugins/llm/sse.c" "../plugins/llm/transport.c" "../plugins/tool/tools.c" "../boards/esp_hi/board.c"
       "../plugins/context/wal.c" "../plugins/context/context.c" "../plugins/context/recall.c" "../plugins/context/sync.c" "../platform/espidf/store.c" ${audio_sources}
       "../plugins/display/display.c" "../boards/esp_hi/display_board.c" "../boards/esp_hi/hardware.c"
  INCLUDE_DIRS "." "../core" "../third_party/cjson" "../platform/espidf" "../plugins/llm" "../plugins/tool" "../boards/esp_hi" "../plugins/context" "../plugins/audio" "../plugins/control" "../plugins/display"
  REQUIRES esp_driver_usb_serial_jtag esp_system esp_wifi esp_event esp_netif nvs_flash esp_partition esp_timer esp_http_client esp-tls mbedtls espressif__led_strip
  PRIV_REQUIRES esp_driver_i2s esp_adc esp_driver_gpio esp_driver_ledc esp_driver_spi esp_lcd agent_speech agent_vad spi_flash)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  AGENT_ENABLE_AUDIO=$<BOOL:${CONFIG_AGENT_AUDIO}> AGENT_PACKED_CLIP=$<BOOL:${AGENT_PACKED_CLIP}>
  CJSON_NESTING_LIMIT=16)
if(AGENT_PACKED_CLIP AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_BACKGROUND_VERIFY))
  message(FATAL_ERROR "Packed clips require the background audio verifier")
endif()
if(AGENT_BACKGROUND_VERIFY AND (NOT CONFIG_AGENT_AUDIO OR CONFIG_FREERTOS_HZ LESS 1000))
  message(FATAL_ERROR "Speech confirmation requires audio and 1000-Hz scheduling")
endif()
if(AGENT_MIC_OVERSAMPLE)
  if(NOT CONFIG_AGENT_AUDIO OR CONFIG_FREERTOS_HZ LESS 1000)
    message(FATAL_ERROR "Microphone oversampling requires audio and 1000-Hz scheduling")
  endif()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_MIC_OVERSAMPLE=1)
endif()
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
set_target_properties(${COMPONENT_LIB} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
get_target_property(agent_sources ${COMPONENT_LIB} SOURCES)
set_source_files_properties(${agent_sources} PROPERTIES COMPILE_OPTIONS "-std=c11")
set_source_files_properties("../platform/espidf/runtime.c" "../platform/espidf/http.c"
  "../platform/espidf/store.c" "../boards/esp_hi/board.c" "../boards/esp_hi/audio_board.c"
  "../boards/esp_hi/control_board.c" "../boards/esp_hi/vad_worker.c" "../boards/esp_hi/phase_clock.c" "../boards/esp_hi/display_board.c"
  PROPERTIES COMPILE_DEFINITIONS "asm=__asm__")
if(CONFIG_AGENT_AUDIO)
  if(NOT AGENT_MIC_ATTEN_DB STREQUAL "12")
    message(FATAL_ERROR "The accepted board baseline uses 12 dB ADC attenuation")
  endif()
  set_property(SOURCE "../boards/esp_hi/audio_board.c" APPEND PROPERTY COMPILE_DEFINITIONS "AGENT_MIC_ATTENUATION=ADC_ATTEN_DB_12")
  set_property(SOURCE "../plugins/audio/decimate.c" APPEND PROPERTY COMPILE_OPTIONS "-O3")
  set_property(SOURCE "../boards/esp_hi/phase.c" "../boards/esp_hi/phase_clock.c"
    "../plugins/audio/clip.c" "../plugins/audio/clip_packed.c" APPEND PROPERTY COMPILE_OPTIONS "-fstack-usage")
endif()
