add_executable(rvn_bench bench_main.cpp)
target_link_libraries(rvn_bench PRIVATE rvn::core rvn_options)
