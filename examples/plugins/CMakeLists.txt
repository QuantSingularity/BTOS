add_library(btos_example_spread_slippage MODULE spread_slippage.c)
set_target_properties(btos_example_spread_slippage PROPERTIES
  PREFIX ""
  OUTPUT_NAME spread_slippage
  C_STANDARD 11
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(btos_example_spread_slippage PRIVATE ${PROJECT_SOURCE_DIR}/include)
