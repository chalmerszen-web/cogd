# Keep vendor code unchanged on disk. The public open API also sends headers;
# this pinned translation-unit adapter exposes only its connection phase.
function(agent_configure_http_preconnect)
  idf_component_get_property(agent_http_target esp_http_client COMPONENT_LIB)
  get_target_property(agent_http_dir ${agent_http_target} SOURCE_DIR)
  set(agent_http_source "${agent_http_dir}/esp_http_client.c")
  file(READ "${agent_http_source}" agent_http_text)
  string(REPLACE "\r\n" "\n" agent_http_text "${agent_http_text}")
  string(SHA256 agent_http_hash "${agent_http_text}")
  if(NOT agent_http_hash STREQUAL "b28179259684e4cadbcaff00127ac50ea5c4477536cca1b4df86590694acdf24")
    message(FATAL_ERROR "Review the pinned HTTP connect-only adapter before changing IDF")
  endif()
  get_target_property(agent_http_sources ${agent_http_target} SOURCES)
  set(agent_http_replaced 0)
  foreach(agent_source IN LISTS agent_http_sources)
    get_filename_component(agent_name "${agent_source}" NAME)
    if(agent_name STREQUAL "esp_http_client.c")
      list(REMOVE_ITEM agent_http_sources "${agent_source}")
      math(EXPR agent_http_replaced "${agent_http_replaced}+1")
    endif()
  endforeach()
  if(NOT agent_http_replaced EQUAL 1)
    message(FATAL_ERROR "Expected one pinned HTTP client implementation")
  endif()
  file(TO_CMAKE_PATH "${agent_http_source}" agent_http_include)
  set(agent_http_wrapper "${CMAKE_BINARY_DIR}/agent_http_preconnect.c")
  file(WRITE "${agent_http_wrapper}" "#include \"${agent_http_include}\"\n"
    "esp_err_t esp_agent_http_client_connect_step(esp_http_client_handle_t client)\n"
    "{\n"
    "    bool saved = client->is_async;\n"
    "    client->is_async = true;\n"
    "    esp_err_t result = esp_http_client_connect(client);\n"
    "    client->is_async = saved;\n"
    "    return result;\n"
    "}\n")
  set_property(TARGET ${agent_http_target} PROPERTY SOURCES ${agent_http_sources} "${agent_http_wrapper}")
endfunction()
