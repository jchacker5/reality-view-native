add_executable(rvn_record rvn_record.cpp)
target_link_libraries(rvn_record PRIVATE rvn::core rvn_options)

add_executable(rvn_can_tx rvn_can_tx.cpp)
target_link_libraries(rvn_can_tx PRIVATE rvn::core rvn_options)

if(RVN_BUILD_APP)
    qt_add_executable(pacing_probe pacing_probe.cpp)
    target_link_libraries(pacing_probe PRIVATE rvn::core Qt6::Gui Qt6::Qml Qt6::Quick rvn_options)
endif()
