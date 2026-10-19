qt_add_executable(window-system-model-test layout_model_test.cpp)
target_link_libraries(window-system-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.model COMMAND window-system-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/model-results.txt,txt)
set_tests_properties(window-system.model PROPERTIES LABELS model)

qt_add_executable(window-system-widget-test workspace_test.cpp)
target_link_libraries(window-system-widget-test PRIVATE WindowSystem::Widgets Qt6::Test)
set_target_properties(window-system-widget-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.widgets COMMAND window-system-widget-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/widget-results.txt,txt)
set_tests_properties(window-system.widgets PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS desktop)

qt_add_executable(window-system-extension-api-test extension_api_test.cpp)
target_link_libraries(window-system-extension-api-test PRIVATE WindowSystem::Widgets Qt6::Test)
target_include_directories(window-system-extension-api-test PRIVATE ../src)
set_target_properties(window-system-extension-api-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.extension-api COMMAND window-system-extension-api-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/extension-api-results.txt,txt)
set_tests_properties(window-system.extension-api PROPERTIES TIMEOUT 60 LABELS widget)

qt_add_executable(window-system-drag-test drag_interaction_test.cpp)
target_link_libraries(window-system-drag-test PRIVATE WindowSystem::Widgets Qt6::Test)
target_include_directories(window-system-drag-test PRIVATE ../src)
set_target_properties(window-system-drag-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.drag-interaction COMMAND window-system-drag-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/drag-results.txt,txt)
set_tests_properties(window-system.drag-interaction PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS desktop)

qt_add_executable(window-system-overlay-fixture fixtures/clickthrough-overlay/main.cpp)
target_link_libraries(window-system-overlay-fixture PRIVATE Qt6::Widgets)
target_compile_features(window-system-overlay-fixture PRIVATE cxx_std_20)
if(WIN32)
    target_link_libraries(window-system-overlay-fixture PRIVATE user32)
endif()

qt_add_executable(window-system-custom-chrome-test custom_chrome_test.cpp)
target_link_libraries(window-system-custom-chrome-test PRIVATE WindowSystem::Widgets Qt6::Test)
target_include_directories(window-system-custom-chrome-test PRIVATE ../src)
set_target_properties(window-system-custom-chrome-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.custom-chrome COMMAND window-system-custom-chrome-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/custom-chrome-results.txt,txt)
set_tests_properties(window-system.custom-chrome PROPERTIES TIMEOUT 60 LABELS widget)
add_dependencies(window-system-drag-test window-system-overlay-fixture)

# Layers follow docs/development/testing-design.md: desktop suites show
# windows or drive native input and run serially; model and widget suites
# create no visible window and may run in parallel.
set(window_system_model_suites pinned-layout pinned-positions)
set(window_system_widget_suites hierarchy-conversion visibility-profile tab-appearance shortcut-settings localization)
set(window_system_desktop_suites pane-settings pane-bar-policy pane-minimum pinned-ui window-placement tab-bar-presentation edge-tab-interaction chrome-interaction caption-strip toolbar-popup toolbar-reset toolbar-policy toolbar-preview sidebar-menu sidebar-presentation presentation compact-ui movement edge-strip drag-presentation strip-drag menu-structure view-state focus-mode multirow-toolbar view-commands peek-size toolbar-profile tab-viewport floating-panel status-bar tab-creation tab-strip-layout popup-menu tab-group shortcut-dispatch memory-lifecycle view-reuse)
function(window_system_add_suite suite label)
    string(REPLACE "-" "_" source_name ${suite})
    qt_add_executable(window-system-${suite}-test ${source_name}_test.cpp)
    target_link_libraries(window-system-${suite}-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_include_directories(window-system-${suite}-test PRIVATE ../src)
    set_target_properties(window-system-${suite}-test PROPERTIES AUTOMOC ON)
    add_test(NAME window-system.${suite} COMMAND window-system-${suite}-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/${suite}-results.txt,txt)
    set_tests_properties(window-system.${suite} PROPERTIES TIMEOUT 60 LABELS ${label})
    if(label STREQUAL "desktop")
        set_tests_properties(window-system.${suite} PROPERTIES RUN_SERIAL TRUE)
    endif()
endfunction()
foreach(label model widget desktop)
    foreach(suite IN LISTS window_system_${label}_suites)
        window_system_add_suite(${suite} ${label})
    endforeach()
endforeach()
qt_add_executable(window-system-tab-ordering-model-test tab_ordering_model_test.cpp)
target_link_libraries(window-system-tab-ordering-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-tab-ordering-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.tab-ordering-model COMMAND window-system-tab-ordering-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/tab-ordering-model-results.txt,txt)
set_tests_properties(window-system.tab-ordering-model PROPERTIES LABELS model)

qt_add_executable(window-system-movement-model-test movement_model_test.cpp)
target_link_libraries(window-system-movement-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-movement-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.movement-model COMMAND window-system-movement-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/movement-model-results.txt,txt)
set_tests_properties(window-system.movement-model PROPERTIES LABELS model)

if(WIN32)
    target_link_libraries(window-system-memory-lifecycle-test PRIVATE psapi user32)
    qt_add_executable(window-system-native-frame-test native_frame_test.cpp)
    target_link_libraries(window-system-native-frame-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_include_directories(window-system-native-frame-test PRIVATE ../src)
    set_target_properties(window-system-native-frame-test PROPERTIES AUTOMOC ON)
    add_test(NAME window-system.native-frame COMMAND window-system-native-frame-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/native-frame-results.txt,txt)
    set_tests_properties(window-system.native-frame PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS desktop)
endif()

# This matrix constructs a real Qt window for each drag/geometry scenario.
set_tests_properties(window-system.pinned-ui PROPERTIES TIMEOUT 120)

# Benchmark statistics only exist when the opt-in benchmark build is enabled.
if(WINDOW_SYSTEM_BUILD_BENCHMARKS)
    qt_add_executable(window-system-benchmark-support-test benchmark_support_test.cpp
        ../benchmarks/benchmark_support.cpp)
    target_link_libraries(window-system-benchmark-support-test PRIVATE Qt6::Widgets Qt6::Test)
    target_include_directories(window-system-benchmark-support-test PRIVATE ../benchmarks)
    target_compile_features(window-system-benchmark-support-test PRIVATE cxx_std_20)
    set_target_properties(window-system-benchmark-support-test PROPERTIES AUTOMOC ON)
    if(WIN32)
        target_link_libraries(window-system-benchmark-support-test PRIVATE psapi)
    endif()
    add_test(NAME window-system.benchmark-support COMMAND window-system-benchmark-support-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/benchmark-support-results.txt,txt)
    set_tests_properties(window-system.benchmark-support PROPERTIES LABELS model)
endif()
