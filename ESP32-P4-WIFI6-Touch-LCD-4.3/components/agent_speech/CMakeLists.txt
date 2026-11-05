get_filename_component(cogd "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(sr "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp-sr")
idf_component_register(SRCS "${cogd}/components/agent_speech/speech_kws.c"
  INCLUDE_DIRS "${cogd}/components/agent_speech" "${sr}/include/esp32p4"
  REQUIRES kws_c11 esp_timer heap)
add_prebuilt_library(agent_p4_audio_processor "${sr}/lib/esp32p4/libesp_audio_processor.a")
target_link_libraries(${COMPONENT_LIB} INTERFACE agent_p4_audio_processor)
target_compile_definitions(${COMPONENT_LIB} PUBLIC AGENT_KWS_C11=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror -fstack-usage)
