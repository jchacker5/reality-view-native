find_package(Qt6 REQUIRED COMPONENTS Quick Test)

add_executable(tst_ui_smoke tst_ui_smoke.cpp)
target_link_libraries(tst_ui_smoke PRIVATE rvn_ui rvn_uiplugin Qt6::Quick Qt6::Test rvn_options)
set_target_properties(tst_ui_smoke PROPERTIES AUTOMOC ON)
# Qt Test's QTRY_* macros mix int and std::chrono internally; GCC flags that under
# -Wconversion inside the macro expansion. Production targets keep the warning.
set_source_files_properties(tst_ui_smoke.cpp PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-conversion>")
add_test(NAME tst_ui_smoke COMMAND tst_ui_smoke)
# Headless: offscreen windowing plus Qt's "null" RHI backend, which runs the whole scene-graph
# pipeline (sync, updatePaintNode, render loop) without needing a GPU on the CI runner.
set_tests_properties(tst_ui_smoke PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=null;QT_QUICK_CONTROLS_STYLE=Basic")
