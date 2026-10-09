option(AGENT_MIC_OVERSAMPLE "32-kHz ADC with anti-aliasing to 16 kHz" ON)
option(AGENT_USER_TEST "Packaged voice test release with a stable version and fast-mode default" OFF)
option(AGENT_CLOCK_BOOT "Boot into the 24-hour HH:MM and corner-seconds clock" OFF)
option(AGENT_MIC_EXACT_CLOCK "Experimental exact C3 ADC clock before conversion" OFF)
set(agent_adc_clock_requires)
if(AGENT_MIC_EXACT_CLOCK)
  if(NOT CONFIG_AGENT_AUDIO OR NOT CONFIG_IDF_TARGET_ESP32C3)
    message(FATAL_ERROR "Exact ADC clock requires C3 audio")
  endif()
  list(APPEND agent_adc_clock_requires esp_hal_ana_conv)
endif()
option(AGENT_PACKED_CLIP "Lossless version3 clip storage" ON)
option(AGENT_VOICE_HEAP_DIAGNOSTICS "Whole voice job allocation minima (diagnostic only)" OFF)
option(AGENT_WS_TIMING "Detailed transport/upload counters and PCM CRC (diagnostic only)" OFF)
option(AGENT_WS_CONNECT_TRACE "Candidate nested TLS-connect clock (diagnostic only)" OFF)
option(AGENT_TLS_PHASE_TRACE "Candidate TLS/crypto wall clocks (diagnostic only)" OFF)
option(AGENT_TLS_ASYM_PERF "Optimize TLS big-integer/RSA arithmetic within the app budget" OFF)
option(AGENT_TLS_PUBLIC_PERF "Use the pinned public MPI path above the hardware limit" OFF)
option(AGENT_RT_DIAGNOSTICS "State-only realtime rejection trace (diagnostic only)" OFF)
option(AGENT_WS_INCREMENTAL "Incremental TLS/WS reader after upgrade buffer drain" ON)
option(AGENT_ENDPOINT_TRACE "USB export of per-frame capture diagnostics" OFF)
option(AGENT_CAPTURE_PROBE "Fixed8s capture with a shadow endpoint (diagnostic only)" OFF)
option(AGENT_KEYWORD_PCM "Bounded USB export of actual C3 keyword input (diagnostic only)" OFF)
if(AGENT_KEYWORD_PCM AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_KWS_C11))
  message(FATAL_ERROR "Keyword PCM observation requires audio and the C11 backend")
endif()
if(AGENT_KWS_WIDE_PROBE AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_KWS_C11))
  message(FATAL_ERROR "Wide resource probe requires audio and the C11 backend")
endif()
if(AGENT_KWS_VERIFIED AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_KWS_C11))
  message(FATAL_ERROR "Verified KWS requires audio and the C11 backend")
endif()
if(AGENT_KWS_GRU_RESOURCE_ONLY AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_KWS_C11 OR AGENT_KEYWORD_PCM))
  message(FATAL_ERROR "Untrained GRU resource mode requires full audio/C11, without legacy keyword metadata")
endif()
if(AGENT_CAPTURE_PROBE AND (NOT AGENT_ENDPOINT_TRACE OR NOT CONFIG_AGENT_AUDIO OR NOT AGENT_BACKGROUND_VERIFY))
  message(FATAL_ERROR "Capture probe requires endpoint trace, audio and background verification")
endif()
option(AGENT_ISOLATED_ASR "Experimental single-input Qwen ASR in classic voice mode" OFF)
option(AGENT_VOICE_SCRATCH_LOANS "Explicit ASR TX-tail and TTS RX-scratch PCM loans" OFF)
if(AGENT_VOICE_SCRATCH_LOANS AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_ISOLATED_ASR))
  message(FATAL_ERROR "Voice scratch loans require audio and isolated ASR")
endif()
option(AGENT_WS_OWNER_PROBE "Three bounded ASR/Omni connection admission cycles, no audio" OFF)
option(AGENT_TEXT_PREFETCH "Independent bounded text candidate during isolated ASR" OFF)
option(AGENT_TLS_COOPERATE "Candidate TLS steps yield while sharing ASR connect priority" OFF)
option(AGENT_VOICE_HTTP_RELEASE "Close completed final-answer HTTP before joining TTS" OFF)
option(AGENT_VOICE_HTTP_FORGET "Free completed final-answer HTTP handles before joining TTS" OFF)
option(AGENT_CONTEXTUAL_CACHED_ACK "Use admitted intent for local fallback and joined final continuity" OFF)
if(AGENT_VOICE_HTTP_FORGET AND NOT AGENT_VOICE_HTTP_RELEASE)
  message(FATAL_ERROR "Final HTTP handle disposal requires final-answer release")
endif()
if(AGENT_VOICE_HTTP_RELEASE AND NOT CONFIG_AGENT_AUDIO)
  message(FATAL_ERROR "Final-answer HTTP release requires audio")
endif()
if(AGENT_TLS_COOPERATE AND NOT AGENT_TEXT_PREFETCH)
  message(FATAL_ERROR "TLS cooperation requires the scoped text candidate")
endif()
option(AGENT_LOCAL_FIRST_PREFETCH "Experimental local lamps skip unused speculative speech" OFF)
option(AGENT_REQUEST_SCRATCH_COMPACT "Size preparation-only request scratch from validated records" OFF)
option(AGENT_TLS_TX_COMPACT "Experimental verified voice route with 2048-byte TLS TX records" OFF)
if(AGENT_TLS_TX_COMPACT)
  if(NOT AGENT_VOICE_HTTP_RELEASE OR NOT AGENT_REQUEST_SCRATCH_COMPACT)
    message(FATAL_ERROR "Compact TLS TX requires the scoped final-HTTP/request route")
  endif()
  if(NOT CONFIG_MBEDTLS_DYNAMIC_BUFFER OR NOT CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN OR
      NOT CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN EQUAL 2048 OR NOT CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN EQUAL 16384)
    message(FATAL_ERROR "Compact TLS TX requires dynamic buffers with OUT2048/IN16384")
  endif()
endif()
if(AGENT_REQUEST_SCRATCH_COMPACT AND NOT AGENT_TEXT_PREFETCH)
  message(FATAL_ERROR "Compact request preparation requires the joined text-prefetch route")
endif()
if(AGENT_LOCAL_FIRST_PREFETCH AND NOT AGENT_TEXT_PREFETCH)
  message(FATAL_ERROR "Local-first speculation requires the text-prefetch route")
endif()
option(AGENT_HANDOFF_PROBE "Fixed-input candidate handoff barriers (diagnostic only)" OFF)
if(AGENT_HANDOFF_PROBE AND NOT AGENT_TEXT_PREFETCH)
  message(FATAL_ERROR "Handoff probe requires text prefetch")
endif()
if(AGENT_TEXT_PREFETCH AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_ISOLATED_ASR OR AGENT_WS_OWNER_PROBE))
  message(FATAL_ERROR "Text candidate requires audio/isolated ASR and no connection probe")
endif()
if(AGENT_WS_CONNECT_TRACE AND NOT AGENT_TEXT_PREFETCH)
  message(FATAL_ERROR "Candidate connect clock requires text prefetch")
endif()
if(AGENT_TLS_PHASE_TRACE AND NOT AGENT_WS_CONNECT_TRACE)
  message(FATAL_ERROR "TLS phase clock requires candidate connect clock")
endif()
if(AGENT_WS_OWNER_PROBE AND (NOT CONFIG_AGENT_AUDIO OR NOT AGENT_ISOLATED_ASR))
  message(FATAL_ERROR "Connection probe requires audio and isolated ASR")
endif()
set(AGENT_TRAINED_WAKE_THRESHOLD "920" CACHE STRING "Validated trained-model wake probability per mille")
if(AGENT_TRAINED_WAKE_THRESHOLD LESS 500 OR AGENT_TRAINED_WAKE_THRESHOLD GREATER 950)
  message(FATAL_ERROR "Trained wake threshold must be in 500..950")
endif()
set(AGENT_MIC_ATTEN_DB "12" CACHE STRING "Microphone ADC attenuation")
set(audio_sources)
if(CONFIG_AGENT_AUDIO)
  set(audio_sources
    "../plugins/audio/audio.c" "../plugins/audio/clip.c" "../plugins/audio/voice.c"
    "../plugins/audio/noise.c" "../plugins/audio/progress.c" "../boards/esp_hi/audio_board.c" "../boards/esp_hi/audio_irq.c"
    "../plugins/audio/ima.c" "../plugins/audio/ack_spool.c"
    "../plugins/audio/song.c" "../plugins/audio/song_synth.c" "../plugins/audio/endpoint.c"
    "../plugins/audio/source_stream.c" "../plugins/audio/source_bound.c"
    "../plugins/audio/confirmation.c" "../plugins/audio/confirmation_tail.c"
    "../plugins/audio/tonal.c" "../boards/esp_hi/phase.c" "../boards/esp_hi/phase_clock.c"
    "../plugins/speech/speech.c" "../plugins/speech/wav.c" "../plugins/speech/qianwen.c" "../plugins/speech/text_pipe.c" "../plugins/speech/asr_end.c" "../plugins/speech/realtime.c"
    "../plugins/speech/draft.c" "../plugins/speech/prefetch.c" "../plugins/speech/ws_reader.c" "../plugins/speech/intent.c" "../plugins/speech/pcm_json.c" "../plugins/speech/voice_history.c"
    "../platform/espidf/speech_ws.c" "../platform/espidf/voice_stream.c" "../platform/espidf/voice_fast.c" "../platform/espidf/capture_radio.c"
    "../core/upload.c")
  if(AGENT_ISOLATED_ASR)
    list(APPEND audio_sources "../plugins/speech/asr_realtime.c" "../plugins/speech/asr_segments.c")
  endif()
  if(AGENT_TEXT_PREFETCH)
    list(APPEND audio_sources "../plugins/speech/candidate.c" "../platform/espidf/voice_candidate.c" "../platform/espidf/engine_memory.c")
  endif()
  if(AGENT_TLS_COOPERATE)
    list(APPEND audio_sources "../platform/espidf/tls_cooperate.c")
  endif()
  if(AGENT_HANDOFF_PROBE)
    list(APPEND audio_sources "../platform/espidf/tcp_probe.c")
    # IDF's private lwIP headers reach RISC-V inline assembly in strict C11.
    set_source_files_properties("../platform/espidf/tcp_probe.c" PROPERTIES COMPILE_DEFINITIONS "asm=__asm__")
  endif()
  if(AGENT_WS_CONNECT_TRACE)
    list(APPEND audio_sources "../platform/espidf/ws_connect_clock.c")
  endif()
  if(AGENT_TLS_PHASE_TRACE)
    list(APPEND audio_sources "../platform/espidf/tls_phase_clock.c")
  endif()
  if(AGENT_MIC_OVERSAMPLE)
    list(APPEND audio_sources "../plugins/audio/decimate.c")
  endif()
  if(AGENT_MIC_EXACT_CLOCK)
    list(APPEND audio_sources "../platform/espidf/adc_clock.c")
  endif()
  if(AGENT_BACKGROUND_VERIFY)
    list(APPEND audio_sources "../boards/esp_hi/vad_worker.c")
  endif()
  if(AGENT_PACKED_CLIP)
    list(APPEND audio_sources "../plugins/audio/clip_packed.c")
  endif()
  if(AGENT_KEYWORD_PCM)
    list(APPEND audio_sources "../plugins/audio/keyword_pcm.c")
  endif()
endif()
if(AGENT_KWS_C11 AND CONFIG_AGENT_AUDIO)
  list(APPEND audio_sources "../platform/espidf/kws_usb.c")
endif()
if(AGENT_VOICE_HEAP_DIAGNOSTICS)
  if(NOT CONFIG_AGENT_AUDIO OR NOT CONFIG_HEAP_USE_HOOKS)
    message(FATAL_ERROR "Voice heap diagnostics require audio and CONFIG_HEAP_USE_HOOKS")
  endif()
  list(APPEND audio_sources "../platform/espidf/voice_heap.c")
endif()
idf_component_register(
  SRCS "app_main.c" "../core/agent.c" "../core/json.c" "../core/crc.c" "../plugins/llm/engine.c"
       "../plugins/control/resources.c" "../plugins/control/plan.c" "../plugins/control/control.c" "../plugins/control/devices.c" "../boards/esp_hi/control_board.c"
       "../third_party/cjson/cJSON.c" "../platform/espidf/runtime.c" "../platform/espidf/http.c"
       "../plugins/llm/deepseek.c" "../plugins/llm/sse.c" "../plugins/llm/transport.c" "../plugins/tool/tools.c" "../boards/esp_hi/board.c"
       "../plugins/context/wal.c" "../plugins/context/context.c" "../plugins/context/recall.c" "../plugins/context/sync.c" "../platform/espidf/store.c" ${audio_sources}
       "../plugins/display/display.c" "../boards/esp_hi/display_board.c" "../boards/esp_hi/hardware.c"
  INCLUDE_DIRS "." "../core" "../third_party/cjson" "../platform/espidf" "../plugins/llm" "../plugins/tool" "../boards/esp_hi" "../plugins/context" "../plugins/audio" "../plugins/control" "../plugins/display" "../plugins/speech"
  REQUIRES esp_driver_usb_serial_jtag esp_system esp_wifi esp_event esp_netif nvs_flash esp_partition esp_timer esp_http_client esp-tls tcp_transport mbedtls espressif__led_strip
  PRIV_REQUIRES esp_driver_i2s esp_adc esp_driver_gpio esp_driver_ledc esp_driver_spi esp_lcd agent_speech agent_vad spi_flash ${agent_adc_clock_requires})
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  AGENT_USER_TEST=$<BOOL:${AGENT_USER_TEST}>
  AGENT_CLOCK_BOOT=$<BOOL:${AGENT_CLOCK_BOOT}>
  AGENT_WAKE_DEFAULT_THRESHOLD=$<IF:$<OR:$<BOOL:${AGENT_KWS_TRAINED}>,$<BOOL:${AGENT_KWS_WIDE_PROBE}>>,${AGENT_TRAINED_WAKE_THRESHOLD},550>
  AGENT_ENABLE_AUDIO=$<BOOL:${CONFIG_AGENT_AUDIO}> AGENT_PACKED_CLIP=$<BOOL:${AGENT_PACKED_CLIP}>
  AGENT_WS_TIMING=$<BOOL:${AGENT_WS_TIMING}>
  AGENT_WS_CONNECT_TRACE=$<BOOL:${AGENT_WS_CONNECT_TRACE}>
  AGENT_TLS_PHASE_TRACE=$<BOOL:${AGENT_TLS_PHASE_TRACE}>
  AGENT_TLS_ASYM_PERF=$<BOOL:${AGENT_TLS_ASYM_PERF}>
  AGENT_TLS_PUBLIC_PERF=$<BOOL:${AGENT_TLS_PUBLIC_PERF}>
  AGENT_TLS_COOPERATE=$<BOOL:${AGENT_TLS_COOPERATE}>
  AGENT_VOICE_HTTP_RELEASE=$<BOOL:${AGENT_VOICE_HTTP_RELEASE}>
  AGENT_VOICE_HTTP_FORGET=$<BOOL:${AGENT_VOICE_HTTP_FORGET}>
  AGENT_CONTEXTUAL_CACHED_ACK=$<BOOL:${AGENT_CONTEXTUAL_CACHED_ACK}>
  AGENT_TLS_TX_COMPACT=$<BOOL:${AGENT_TLS_TX_COMPACT}>
  AGENT_RT_DIAGNOSTICS=$<BOOL:${AGENT_RT_DIAGNOSTICS}>
  AGENT_RT_AUTOVAD=0
  AGENT_WS_INCREMENTAL=$<BOOL:${AGENT_WS_INCREMENTAL}>
  AGENT_ENDPOINT_TRACE=$<BOOL:${AGENT_ENDPOINT_TRACE}>
  AGENT_CAPTURE_PROBE=$<BOOL:${AGENT_CAPTURE_PROBE}>
  AGENT_ISOLATED_ASR=$<AND:$<BOOL:${AGENT_ISOLATED_ASR}>,$<BOOL:${CONFIG_AGENT_AUDIO}>>
  AGENT_VOICE_SCRATCH_LOANS=$<BOOL:${AGENT_VOICE_SCRATCH_LOANS}>
  AGENT_WS_OWNER_PROBE=$<BOOL:${AGENT_WS_OWNER_PROBE}>
  AGENT_TEXT_PREFETCH=$<BOOL:${AGENT_TEXT_PREFETCH}>
  AGENT_LOCAL_FIRST_PREFETCH=$<BOOL:${AGENT_LOCAL_FIRST_PREFETCH}>
  AGENT_REQUEST_SCRATCH_COMPACT=$<BOOL:${AGENT_REQUEST_SCRATCH_COMPACT}>
  AGENT_HANDOFF_PROBE=$<BOOL:${AGENT_HANDOFF_PROBE}>
  AGENT_RT_TEXT_REUSE=$<BOOL:${AGENT_TEXT_PREFETCH}>
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
if(AGENT_MIC_EXACT_CLOCK)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_MIC_EXACT_CLOCK=1)
  idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=adc_hal_digi_controller_config" APPEND)
  set_source_files_properties("../platform/espidf/adc_clock.c" PROPERTIES COMPILE_DEFINITIONS "asm=__asm__;typeof=__typeof__")
endif()
if(AGENT_WS_CONNECT_TRACE)
  idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_transport_connect" APPEND)
endif()
if(AGENT_TLS_PHASE_TRACE)
  foreach(tls_symbol IN ITEMS esp_create_mbedtls_handle esp_mbedtls_handshake
      psa_generate_key psa_raw_key_agreement psa_verify_hash mbedtls_mpi_exp_mod_soft)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=${tls_symbol}" APPEND)
  endforeach()
endif()
if(AGENT_VOICE_HEAP_DIAGNOSTICS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_VOICE_HEAP_DIAGNOSTICS=1)
  set_source_files_properties("../platform/espidf/voice_heap.c" PROPERTIES COMPILE_DEFINITIONS "asm=__asm__")
endif()
if(AGENT_TLS_ASYM_PERF)
  if(NOT CONFIG_MBEDTLS_TLS_CLIENT_ONLY OR NOT CONFIG_MBEDTLS_COMPILER_OPTIMIZATION_SIZE)
    message(FATAL_ERROR "Targeted TLS optimization requires client-only and global size optimization")
  endif()
  # Only three arithmetic units need -O2. Keep protocol/other crypto at -Os;
  # do not modify vendor sources, TLS policies or firmware data capacities.
  get_target_property(agent_tls_builtin_dir builtin SOURCE_DIR)
  foreach(agent_tls_source IN ITEMS bignum.c bignum_core.c rsa.c)
    set(agent_tls_source_path "${agent_tls_builtin_dir}/src/${agent_tls_source}")
    if(NOT EXISTS "${agent_tls_source_path}")
      message(FATAL_ERROR "Pinned TLS arithmetic source missing: ${agent_tls_source}")
    endif()
    set_property(SOURCE "${agent_tls_source_path}" DIRECTORY "${agent_tls_builtin_dir}"
      APPEND PROPERTY COMPILE_OPTIONS "-O2")
  endforeach()
endif()
if(AGENT_TLS_PUBLIC_PERF)
  if(NOT CONFIG_MBEDTLS_HARDWARE_MPI OR NOT CONFIG_MBEDTLS_LARGE_KEY_SOFTWARE_MPI)
    message(FATAL_ERROR "Public MPI optimization requires the hardware/software MPI configuration")
  endif()
  include("${CMAKE_CURRENT_LIST_DIR}/../cmake/tls_public_mpi.cmake")
  agent_configure_public_mpi(builtin)
  target_sources(${COMPONENT_LIB} PRIVATE "../platform/espidf/tls_public_mpi.c")
endif()
set_target_properties(${COMPONENT_LIB} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
if(CONFIG_AGENT_AUDIO)
  # A PCM/Base64 frame fits 1536 bytes. The default half-window select
  # threshold stalls this duplex stream while usable TCP space remains.
  # The queue stays bounded by the configured send buffer; no UDP/replay.
  idf_component_get_property(agent_lwip_component lwip COMPONENT_LIB)
  target_compile_definitions(${agent_lwip_component} PRIVATE TCP_SNDLOWAT=1536)
endif()
get_target_property(agent_sources ${COMPONENT_LIB} SOURCES)
set_source_files_properties(${agent_sources} PROPERTIES COMPILE_OPTIONS "-std=c11")
# Optimize cross-file application control/protocol code, not vendor libraries
# or real-time sampling/DSP translation units. Fat objects keep archive tools
# compatible; final code is still bounded by the unchanged flash guard.
option(AGENT_APP_LTO "Link-time optimization of application control code" ON)
if(AGENT_APP_LTO)
  target_compile_options(${COMPONENT_LIB} PRIVATE -flto -ffat-lto-objects)
  idf_build_set_property(LINK_OPTIONS "-flto" APPEND)
  set_property(SOURCE "../boards/esp_hi/audio_irq.c"
    "../plugins/audio/decimate.c" "../plugins/audio/voice.c" "../plugins/audio/tonal.c"
    "../plugins/audio/source_stream.c" "../plugins/audio/source_bound.c"
    APPEND PROPERTY COMPILE_OPTIONS "-fno-lto")
endif()
set_source_files_properties("../platform/espidf/runtime.c" "../platform/espidf/http.c" "../platform/espidf/speech_ws.c" "../platform/espidf/voice_stream.c" "../platform/espidf/voice_fast.c" "../platform/espidf/voice_candidate.c" "../platform/espidf/capture_radio.c"
  "../platform/espidf/tls_phase_clock.c"
  "../platform/espidf/store.c" "../boards/esp_hi/board.c" "../boards/esp_hi/audio_board.c" "../boards/esp_hi/audio_irq.c"
  "../platform/espidf/tls_cooperate.c"
  "../boards/esp_hi/control_board.c" "../boards/esp_hi/vad_worker.c" "../boards/esp_hi/phase_clock.c" "../boards/esp_hi/display_board.c"
  PROPERTIES COMPILE_DEFINITIONS "asm=__asm__")
if(CONFIG_AGENT_AUDIO)
  if(NOT AGENT_MIC_ATTEN_DB STREQUAL "12")
    message(FATAL_ERROR "The accepted board baseline uses 12 dB ADC attenuation")
  endif()
  set_property(SOURCE "../boards/esp_hi/audio_board.c" APPEND PROPERTY COMPILE_DEFINITIONS "AGENT_MIC_ATTENUATION=ADC_ATTEN_DB_12")
  set_property(SOURCE "../plugins/audio/decimate.c" APPEND PROPERTY COMPILE_OPTIONS "-O3")
  # These per-sample integer filters keep identical arithmetic/state at -O3;
  # inlining constant biquad coefficients avoids repeated generic calls.
  set_property(SOURCE "../plugins/audio/voice.c" "../plugins/audio/tonal.c"
    "../plugins/audio/source_stream.c" "../plugins/audio/source_bound.c"
    APPEND PROPERTY COMPILE_OPTIONS "-O3")
  set_property(SOURCE "../boards/esp_hi/phase.c" "../boards/esp_hi/phase_clock.c"
    "../plugins/audio/clip.c" "../plugins/audio/clip_packed.c" APPEND PROPERTY COMPILE_OPTIONS "-fstack-usage")
endif()
