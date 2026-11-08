idf_component_register(SRCS "aoq_token.cpp" "aoq_wire.cpp" "aoq_objects.cpp" "aoq_media_control.cpp" "aoq_video.cpp"
    "aoq_session.cpp" "aoq_prefetch.cpp" "aoq_conversation.cpp" "aoq_speech.cpp" "aoq_tts_trial.cpp" "aoq_asr_trial.cpp" "aoq_dialogue_trial.cpp" "aoq_pcm_fixture.cpp" "aoq_tts_play.c" "aoq_probe.cpp"
    INCLUDE_DIRS "." "../../../third_party/cjson"
    REQUIRES esp_http3 esp_http_client mbedtls esp_timer esp_system lwip esp_audio_codec)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
