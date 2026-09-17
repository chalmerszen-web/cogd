get_filename_component(ten_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ten_vendor "${ten_root}/third_party/ten-vad/vendor")
set(ten_kernel "${CMAKE_CURRENT_LIST_DIR}/kernel")
set(ten_tables "${ten_root}/third_party/ten-vad/generated")
include("${CMAKE_CURRENT_LIST_DIR}/sources.lock.cmake")
set(ten_vendor_sources)
foreach(name aed ten_vad biquad pitch_est stft fftw)
  list(APPEND ten_vendor_sources "${ten_vendor}/${name}.c")
endforeach()
set(ten_owned_sources "${CMAKE_CURRENT_LIST_DIR}/vad_backend.c" "${CMAKE_CURRENT_LIST_DIR}/ten_memory.c"
  "${ten_kernel}/model.c" "${ten_kernel}/fixed.c")
set(ten_definitions TEN_DEVICE TEN_CACHE_ROWS=0 TEN_FIXED_DSP TEN_FIXED8 TEN_BINARY_SCALE
  TEN_NARROW_DSP TEN_BINARY_GUARDS TEN_NARROW_CONVERT TEN_PITCH_GUARDS)
foreach(source IN LISTS ten_vendor_sources)
  set_source_files_properties("${source}" PROPERTIES COMPILE_DEFINITIONS
    "malloc=agent_ten_malloc;calloc=agent_ten_calloc;free=agent_ten_free")
endforeach()
