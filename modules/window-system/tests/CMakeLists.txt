qt_add_executable(window-system-model-test layout_model_test.cpp)
target_link_libraries(window-system-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.model COMMAND window-system-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/model-results.txt,txt)

qt_add_executable(window-system-widget-test workspace_test.cpp)
target_link_libraries(window-system-widget-test PRIVATE WindowSystem::Widgets Qt6::Test)
set_target_properties(window-system-widget-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.widgets COMMAND window-system-widget-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/widget-results.txt,txt)
set_tests_properties(window-system.widgets PROPERTIES TIMEOUT 60)

qt_add_executable(window-system-drag-test drag_interaction_test.cpp)
target_link_libraries(window-system-drag-test PRIVATE WindowSystem::Widgets Qt6::Test)
target_include_directories(window-system-drag-test PRIVATE ../src)
set_target_properties(window-system-drag-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.drag-interaction COMMAND window-system-drag-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/drag-results.txt,txt)
set_tests_properties(window-system.drag-interaction PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

qt_add_executable(window-system-overlay-fixture fixtures/clickthrough-overlay/main.cpp)
target_link_libraries(window-system-overlay-fixture PRIVATE Qt6::Widgets)
target_compile_features(window-system-overlay-fixture PRIVATE cxx_std_20)
if(WIN32)
    target_link_libraries(window-system-overlay-fixture PRIVATE user32)
endif()
add_dependencies(window-system-drag-test window-system-overlay-fixture)

foreach(suite pane-settings pane-bar-policy window-placement tab-bar-presentation edge-tab-interaction chrome-interaction caption-strip toolbar-popup toolbar-reset toolbar-policy toolbar-preview sidebar-menu sidebar-presentation presentation compact-ui movement edge-strip drag-presentation strip-drag menu-structure view-state focus-mode multirow-toolbar view-commands peek-size toolbar-profile tab-viewport floating-panel status-bar tab-creation visibility-profile tab-strip-layout tab-appearance popup-menu tab-group shortcut-dispatch shortcut-settings localization)
    string(REPLACE "-" "_" source_name ${suite})
    qt_add_executable(window-system-${suite}-test ${source_name}_test.cpp)
    target_link_libraries(window-system-${suite}-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_include_directories(window-system-${suite}-test PRIVATE ../src)
    set_target_properties(window-system-${suite}-test PROPERTIES AUTOMOC ON)
    add_test(NAME window-system.${suite} COMMAND window-system-${suite}-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/${suite}-results.txt,txt)
    set_tests_properties(window-system.${suite} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()
qt_add_executable(window-system-tab-ordering-model-test tab_ordering_model_test.cpp)
target_link_libraries(window-system-tab-ordering-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-tab-ordering-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.tab-ordering-model COMMAND window-system-tab-ordering-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/tab-ordering-model-results.txt,txt)

qt_add_executable(window-system-movement-model-test movement_model_test.cpp)
target_link_libraries(window-system-movement-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-movement-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.movement-model COMMAND window-system-movement-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/movement-model-results.txt,txt)

if(WIN32)
    qt_add_executable(window-system-native-frame-test native_frame_test.cpp)
    target_link_libraries(window-system-native-frame-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_include_directories(window-system-native-frame-test PRIVATE ../src)
    set_target_properties(window-system-native-frame-test PROPERTIES AUTOMOC ON)
    add_test(NAME window-system.native-frame COMMAND window-system-native-frame-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/native-frame-results.txt,txt)
    set_tests_properties(window-system.native-frame PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
