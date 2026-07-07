add_executable(btos main.cpp)
target_link_libraries(btos PRIVATE btos_lib nlohmann_json::nlohmann_json
  btos_warnings btos_sanitizers)
