get_filename_component(p4 "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(sr "${p4}/managed_components/espressif__esp-sr")
idf_component_register(SRCS "probe.c" "${sr}/src/model_path.c"
    "${sr}/src/esp_mn_speech_commands.c" "${sr}/src/esp_sr_debug.c"
    INCLUDE_DIRS "." "${sr}/include/esp32p4" "${sr}/src/include"
    REQUIRES espressif__esp-dl espressif__dl_fft espressif__cjson esp_driver_uart esp_timer esp_pm heap spiffs esp_partition
    EMBED_FILES "models.bin")
set_source_files_properties(probe.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
set(probe_libraries)
foreach(name dl_lib c_speech_features multinet fst flite_g2p hufzip)
    add_prebuilt_library(probe_${name} "${sr}/lib/esp32p4/lib${name}.a")
    list(APPEND probe_libraries probe_${name})
endforeach()
idf_component_get_property(probe_fft espressif__dl_fft COMPONENT_LIB)
idf_component_get_property(probe_dl espressif__esp-dl COMPONENT_LIB)
target_link_libraries(${COMPONENT_LIB} PUBLIC
    "$<LINK_GROUP:RESCAN,probe_dl_lib,probe_c_speech_features,probe_multinet,probe_fst,probe_flite_g2p,probe_hufzip,${probe_fft},${probe_dl}>")
