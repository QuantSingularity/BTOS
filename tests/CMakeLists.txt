add_executable(btos_tests
  test_core.cpp
  test_data.cpp
  test_data_regression.cpp
  test_exec_portfolio.cpp
  test_engine_opt.cpp
  test_plugin_so.cpp
)
target_link_libraries(btos_tests PRIVATE btos_lib GTest::gtest GTest::gtest_main
  btos_warnings btos_sanitizers)
target_compile_definitions(btos_tests PRIVATE
  BTOS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

if(TARGET btos_example_spread_slippage)
  add_dependencies(btos_tests btos_example_spread_slippage)
  target_compile_definitions(btos_tests PRIVATE
    BTOS_EXAMPLE_PLUGIN_SO="$<TARGET_FILE:btos_example_spread_slippage>")
endif()

add_library(btos_property_tests OBJECT test_property.cpp)
target_link_libraries(btos_property_tests PRIVATE btos_lib GTest::gtest rapidcheck rapidcheck_gtest
  btos_sanitizers)
target_sources(btos_tests PRIVATE $<TARGET_OBJECTS:btos_property_tests>)
target_link_libraries(btos_tests PRIVATE rapidcheck rapidcheck_gtest)

include(GoogleTest)
add_test(NAME btos_all_tests COMMAND btos_tests)
