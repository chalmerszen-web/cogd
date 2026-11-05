idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "."
  EMBED_FILES "../../video-native-probe/main/samples/wan3-high-head.h264"
  REQUIRES p4_media esp_timer esp_pm)
