idf_component_register(SRCS "p4_audio.c" INCLUDE_DIRS "."
  REQUIRES waveshare__esp32_p4_wifi6_touch_lcd_4_3 esp_driver_uart esp_driver_i2s
  espressif__esp_codec_dev espressif__esp_wifi_remote espressif__esp_hosted)
