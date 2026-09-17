option(AGENT_BACKGROUND_VERIFY "Bounded Flash-backed TEN speech confirmation" OFF)
if(NOT AGENT_BACKGROUND_VERIFY)
  idf_component_register()
  return()
endif()
if(NOT IDF_TARGET STREQUAL "esp32c3")
  message(FATAL_ERROR "Background verifier has only an ESP32-C3 port")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")
foreach(path "${ten_vendor}/aed.c" "${ten_tables}/model_fixed8_data.h")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Pinned TEN runtime sources are missing")
  endif()
endforeach()
set(rvfp "${ten_root}/third_party/rvfplib/src/asm/ieee754")
set(rvfp_sources "${rvfp}/__addsf3.S" "${rvfp}/__mulsf3.S" "${rvfp}/__divsf3.S")
set(rvfp_definitions PERFORMANCE=1)
set(rvfp_renames)
foreach(op addsf3 subsf3 mulsf3 divsf3)
  list(APPEND rvfp_definitions "__${op}=agent_ten_${op}")
  list(APPEND rvfp_renames "--redefine-sym" "__${op}=agent_ten_${op}")
endforeach()
set_source_files_properties(${rvfp_sources} PROPERTIES COMPILE_DEFINITIONS "${rvfp_definitions}")
idf_component_register(SRCS ${ten_owned_sources} ${ten_vendor_sources} ${rvfp_sources}
  INCLUDE_DIRS "." "${ten_root}/core"
  PRIV_INCLUDE_DIRS "${ten_vendor}" "${ten_kernel}" "${ten_tables}")
target_compile_definitions(${COMPONENT_LIB} PUBLIC AGENT_BACKGROUND_VERIFY=1)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${ten_definitions})
# The optional arena borrows byte storage whose active use changes under a
# mutex. Keep alias-based optimizations out of this GCC-specific adapter.
target_compile_options(${COMPONENT_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:C>:-std=c11;-O3;-ffp-contract=off;-fno-strict-aliasing;-fstack-usage>")
set_source_files_properties(${ten_owned_sources} PROPERTIES COMPILE_OPTIONS "-std=c11;-Wall;-Wextra;-Werror")
# Rename this archive's arithmetic references only. Agent/SDK float helpers
# continue resolving normally; no executable-wide --wrap is introduced.
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ${rvfp_renames} "$<TARGET_FILE:${COMPONENT_LIB}>"
  VERBATIM)
