pybind11_add_module(_btos src/btos_bindings.cpp)
target_link_libraries(_btos PRIVATE btos_lib)
target_include_directories(_btos PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(_btos PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pybtos/btos)
