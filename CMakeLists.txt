cmake_minimum_required(VERSION 3.25)
project(btos VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BTOS_BUILD_TESTS "Build unit, integration and property tests" ON)
option(BTOS_BUILD_BENCH "Build Google Benchmark suite" ON)
option(BTOS_BUILD_EXAMPLE_PLUGINS "Build the out-of-tree example plugins" ON)
option(BTOS_BUILD_PYTHON "Build the pybind11 Python bindings" OFF)
option(BTOS_SANITIZE_ADDRESS "Build with AddressSanitizer + UBSan" OFF)
option(BTOS_SANITIZE_THREAD "Build with ThreadSanitizer" OFF)
option(BTOS_DEBUG_ACCOUNTING "Check the accounting identity after every fill" OFF)

add_library(btos_warnings INTERFACE)
target_compile_options(btos_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

add_library(btos_sanitizers INTERFACE)
if(BTOS_SANITIZE_ADDRESS)
  target_compile_options(btos_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(btos_sanitizers INTERFACE -fsanitize=address,undefined)
endif()
if(BTOS_SANITIZE_THREAD)
  target_compile_options(btos_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(btos_sanitizers INTERFACE -fsanitize=thread)
endif()

include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
  URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

find_package(Threads REQUIRED)

if(BTOS_BUILD_TESTS)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  FetchContent_Declare(rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG ff6af6fc683159deb51c543b065eba14dfcf329b
  )
  set(RC_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(RC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(rapidcheck)
endif()

if(BTOS_BUILD_BENCH)
  FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
    URL_HASH SHA256=6bc180a57d23d4d9515519f92b0c83d61b05b5bab188961f36ac7b06b0d9e9ce
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()

if(BTOS_DEBUG_ACCOUNTING)
  add_compile_definitions(BTOS_FORCE_ACCOUNTING_CHECKS)
endif()

if(BTOS_BUILD_PYTHON)
  FetchContent_Declare(pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz
    URL_HASH SHA256=e08cb87f4773da97fa7b5f035de8763abc656d87d5773e62f6da0587d1f0ec20
  )
  FetchContent_MakeAvailable(pybind11)
endif()

add_subdirectory(src)
add_subdirectory(apps/btos_cli)

if(BTOS_BUILD_EXAMPLE_PLUGINS)
  add_subdirectory(examples/plugins)
endif()

if(BTOS_BUILD_PYTHON)
  add_subdirectory(python ${CMAKE_BINARY_DIR}/python_build)
endif()

if(BTOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(BTOS_BUILD_BENCH)
  add_subdirectory(bench)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS btos_lib
        EXPORT btosTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT btosTargets NAMESPACE btos:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/btos)
write_basic_package_version_file(btosConfigVersion.cmake COMPATIBILITY SameMajorVersion)
configure_package_config_file(cmake/btosConfig.cmake.in btosConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/btos)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/btosConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/btosConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/btos)
