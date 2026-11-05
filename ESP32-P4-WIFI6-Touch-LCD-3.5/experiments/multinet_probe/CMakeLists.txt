cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32p4)
set(PROJECT_VER "0.0.1-mn-probe")
get_filename_component(p4 "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${p4}/managed_components/espressif__esp-dl"
    "${p4}/managed_components/espressif__dl_fft" "${p4}/managed_components/espressif__cjson"
    "${p4}/managed_components/espressif__esp_new_jpeg")
set(COMPONENTS main)
# The ESP cross toolchain uses GNU ld, but Generic CMake does not advertise
# RESCAN automatically. Keep the mutually dependent vendor archives together.
set(CMAKE_LINK_GROUP_USING_RESCAN "LINKER:--start-group" "LINKER:--end-group")
set(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p4_multinet_probe)
