find_package(SQLite3 REQUIRED)

add_library(btos_lib STATIC
  core/time.cpp
  core/event_log.cpp
  data/data.cpp
  data/btosd.cpp
  data/validate_synthetic.cpp
  exec/order_book.cpp
  exec/matching_engine.cpp
  portfolio/portfolio.cpp
  strategy/reference.cpp
  risk/risk.cpp
  opt/optimizer.cpp
  opt/advanced.cpp
  opt/validation.cpp
  opt/experiment_store.cpp
  analytics/metrics.cpp
  analytics/report.cpp
  plugin_registry.cpp
  engine.cpp
)
add_library(btos::lib ALIAS btos_lib)

target_include_directories(btos_lib PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(btos_lib
  PUBLIC Threads::Threads
  PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
          $<BUILD_INTERFACE:SQLite::SQLite3>
          ${CMAKE_DL_LIBS}
          $<BUILD_INTERFACE:btos_warnings>
          $<BUILD_INTERFACE:btos_sanitizers>)
