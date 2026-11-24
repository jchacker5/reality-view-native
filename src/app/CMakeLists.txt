# The QML module lives in a static library so the app and the UI smoke test share it.
qt_add_library(rvn_ui STATIC)

if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(rvn_ui
    URI RealityView
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/RealityView
    SOURCES
        app_context.cpp app_context.hpp
        frame_monitor.cpp frame_monitor.hpp
        sparkline_item.cpp sparkline_item.hpp
        vehicle_scene_item.cpp vehicle_scene_item.hpp
        vehicle_view_model.cpp vehicle_view_model.hpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Card.qml
        qml/StatRow.qml
        qml/PillButton.qml
        qml/HoldButton.qml
        qml/DriverInput.qml
        qml/StatusStrip.qml
        qml/SceneView.qml
        qml/DriveCard.qml
        qml/PerformanceCard.qml
        qml/PipelineCard.qml
        qml/TelemetryCard.qml
)

target_include_directories(rvn_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rvn_ui
    PUBLIC rvn::core Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2
    PRIVATE rvn_options)

qt_add_executable(reality-view-native main.cpp)
target_compile_definitions(reality-view-native PRIVATE RVN_VERSION="${PROJECT_VERSION}")
target_link_libraries(reality-view-native PRIVATE rvn_ui rvn_uiplugin rvn_options)
set_target_properties(reality-view-native PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER dev.realityview.native
    MACOSX_BUNDLE_BUNDLE_NAME "Reality View Native"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE)
