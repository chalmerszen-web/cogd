idf_build_get_property(p4_aoq_enabled AGENT_P4_AOQ_PROBE)
idf_build_get_property(p4_aec_bench_enabled AGENT_P4_AOQ_PROBE)
idf_build_get_property(p4_communication_mode COGD_COMM_MODE)
if(p4_communication_mode STREQUAL "AOQ")
  set(p4_aoq_enabled ON)
endif()
set(p4_aoq_camera_sources "")
if(p4_aoq_enabled)
  list(APPEND p4_aoq_camera_sources "p4_video_capture.cpp" "p4_audio_frontend.c" "p4_status_voice.cpp")
  set(p4_status_assets "p4_status_voice.bin")
endif()
if(p4_aec_bench_enabled)
  list(APPEND p4_aoq_camera_sources "p4_aec_bench.c")
endif()
idf_component_register(SRCS "p4_audio.c" "p4_vision.cpp" "p4_cloud.cpp" "p4_wifi.cpp" "p4_diag.c" "../../../plugins/model_usage.c" ${p4_aoq_camera_sources} INCLUDE_DIRS "." "../../../third_party/cjson"
  EMBED_TXTFILES "qianwen_catalog.json"
  EMBED_FILES ${p4_status_assets}
  REQUIRES waveshare__esp32_p4_wifi6_touch_lcd_3_5 esp_driver_uart esp_driver_i2s
  espressif__esp_codec_dev espressif__esp_wifi_remote espressif__esp_hosted
  espressif__esp_video espressif__human_face_detect espressif__pedestrian_detect
  esp_http_client esp-tls tcp_transport esp_driver_jpeg esp_timer p4_media esp_partition nvs_flash esp_event esp_wifi
  PRIV_REQUIRES espressif__esp-sr espressif__esp_audio_codec esp_app_format)
target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_BOARD_P4=1)
if(p4_aec_bench_enabled)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_P4_AEC_BENCH=1)
endif()
if(p4_communication_mode STREQUAL "AOQ")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_P4_AOQ_DUPLEX=1)
endif()
if(p4_aoq_enabled)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_P4_AOQ_CAMERA=1)
endif()

# Older vision packages omit esp-dl's new compile requirements. Register them
# alongside esp-sr, so audio pruning cannot remove the vision operators.
idf_component_get_property(p4_espdl espressif__esp-dl COMPONENT_DIR)
include("${p4_espdl}/cmake/compile_register.cmake")
esp_dl_compile_add_yml("${CMAKE_CURRENT_LIST_DIR}/vision.compile_req.yml")
