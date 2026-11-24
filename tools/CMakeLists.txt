add_executable(rvn_record rvn_record.cpp)
target_link_libraries(rvn_record PRIVATE rvn::core rvn_options)
