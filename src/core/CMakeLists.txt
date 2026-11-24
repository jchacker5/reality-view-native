add_library(rvn_core STATIC
    drive_simulator.cpp
    frame_stats.cpp
    process_stats.cpp
    replay_source.cpp
    road.cpp
    scene_geometry.cpp
    telemetry_consumer.cpp
    telemetry_csv.cpp
    telemetry_worker.cpp
    vehicle_model.cpp
)
add_library(rvn::core ALIAS rvn_core)

target_include_directories(rvn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rvn_core
    PUBLIC Threads::Threads
    PRIVATE rvn_options)
target_compile_features(rvn_core PUBLIC cxx_std_20)
