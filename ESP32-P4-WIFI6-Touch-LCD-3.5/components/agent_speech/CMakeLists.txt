get_filename_component(cogd "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(sr "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp-sr")
if(AGENT_P4_WAKENET10)
  idf_component_register(SRCS "speech_wakenet.c"
    INCLUDE_DIRS "${cogd}/components/agent_speech"
    REQUIRES esp_timer heap
    PRIV_REQUIRES espressif__esp-sr)
  set(model "${CMAKE_CURRENT_BINARY_DIR}/wake_model.bin")
  execute_process(COMMAND ${PYTHON} "${cogd}/tools/pack_wake_model.py"
    "${sr}" "${model}" "wn10_nihaoxiaozhi" COMMAND_ERROR_IS_FATAL ANY)
  target_add_binary_data(${COMPONENT_LIB} "${model}" BINARY)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC AGENT_P4_WAKENET10=1 AGENT_BOARD_P4=1)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE asm=__asm__)
  target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror -fstack-usage)
  return()
endif()
idf_component_register(SRCS "${cogd}/components/agent_speech/speech_kws.c"
  INCLUDE_DIRS "${cogd}/components/agent_speech" "${sr}/include/esp32p4"
  # IDF discovers requirements before project options are visible. Keep the
  # official provider in both branches; unused archives add no runtime data.
  REQUIRES kws_c11 esp_timer heap
  PRIV_REQUIRES espressif__esp-sr)
add_prebuilt_library(agent_p4_audio_processor "${sr}/lib/esp32p4/libesp_audio_processor.a")
target_link_libraries(${COMPONENT_LIB} INTERFACE agent_p4_audio_processor)
target_compile_definitions(${COMPONENT_LIB} PUBLIC AGENT_KWS_C11=1 AGENT_BOARD_P4=1)
option(AGENT_P4_KWS_NS "P4 keyword-only10ms medium WebRTC noise suppression" OFF)
if(AGENT_P4_KWS_NS)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC AGENT_P4_KWS_NS=1)
endif()
option(AGENT_KEYWORD_PCM "Export live P4 KWS input over 921600-baud UART (diagnostic only)" OFF)
if(AGENT_KEYWORD_PCM)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC AGENT_KEYWORD_PCM=1)
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror -fstack-usage)
