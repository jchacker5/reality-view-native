function(rvn_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE rvn::core Qt6::Test rvn_options)
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    # Qt Test's QTRY_* macros mix int and std::chrono internally; GCC flags that under
    # -Wconversion inside the macro expansion. Production targets keep the warning.
    # Source-level options are emitted after interface options, so this reliably wins.
    set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-conversion>")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

rvn_add_test(tst_spsc_ring_buffer core/tst_spsc_ring_buffer.cpp)
rvn_add_test(tst_geometry core/tst_geometry.cpp)
rvn_add_test(tst_vehicle_model core/tst_vehicle_model.cpp)
rvn_add_test(tst_road core/tst_road.cpp)
rvn_add_test(tst_drive_simulator core/tst_drive_simulator.cpp)
rvn_add_test(tst_telemetry_pipeline core/tst_telemetry_pipeline.cpp)
rvn_add_test(tst_frame_stats core/tst_frame_stats.cpp)
rvn_add_test(tst_telemetry_csv core/tst_telemetry_csv.cpp)
rvn_add_test(tst_scene_geometry core/tst_scene_geometry.cpp)

if(RVN_BUILD_APP)
    add_subdirectory(app)
endif()
