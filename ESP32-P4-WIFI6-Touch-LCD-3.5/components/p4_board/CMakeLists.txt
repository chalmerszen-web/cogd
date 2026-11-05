idf_component_register(SRCS "p4_audio.c" "p4_vision.cpp" "p4_cloud.cpp" "p4_wifi.cpp" "../../../plugins/model_usage.c" INCLUDE_DIRS "." "../../../third_party/cjson"
  EMBED_TXTFILES "qianwen_catalog.json"
  REQUIRES waveshare__esp32_p4_wifi6_touch_lcd_3_5 esp_driver_uart esp_driver_i2s
  espressif__esp_codec_dev espressif__esp_wifi_remote espressif__esp_hosted
  espressif__esp_video espressif__human_face_detect espressif__pedestrian_detect
  esp_http_client esp-tls tcp_transport esp_driver_jpeg esp_timer p4_media esp_partition nvs_flash esp_event esp_wifi
  PRIV_REQUIRES espressif__esp-sr)
target_compile_definitions(${COMPONENT_LIB} PRIVATE AGENT_BOARD_P4=1)

# Older vision packages omit esp-dl's new compile requirements. Register them
# alongside esp-sr, so audio pruning cannot remove the vision operators.
idf_component_get_property(p4_espdl espressif__esp-dl COMPONENT_DIR)
include("${p4_espdl}/cmake/compile_register.cmake")
esp_dl_compile_add_yml("${CMAKE_CURRENT_LIST_DIR}/vision.compile_req.yml")
