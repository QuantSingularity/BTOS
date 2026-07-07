add_executable(btos_bench bench_main.cpp)
target_link_libraries(btos_bench PRIVATE btos_lib benchmark::benchmark btos_warnings btos_sanitizers)
