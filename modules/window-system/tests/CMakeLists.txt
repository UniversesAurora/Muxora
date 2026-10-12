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

foreach(suite pane-settings pane-bar-policy window-placement tab-bar-presentation edge-tab-interaction chrome-interaction caption-strip toolbar-popup toolbar-reset toolbar-policy sidebar-menu presentation)
    string(REPLACE "-" "_" source_name ${suite})
    qt_add_executable(window-system-${suite}-test ${source_name}_test.cpp)
    target_link_libraries(window-system-${suite}-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_include_directories(window-system-${suite}-test PRIVATE ../src)
    set_target_properties(window-system-${suite}-test PROPERTIES AUTOMOC ON)
    add_test(NAME window-system.${suite} COMMAND window-system-${suite}-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/${suite}-results.txt,txt)
    set_tests_properties(window-system.${suite} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()

if(WIN32)
    qt_add_executable(window-system-native-frame-test native_frame_test.cpp)
    target_link_libraries(window-system-native-frame-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_include_directories(window-system-native-frame-test PRIVATE ../src)
    set_target_properties(window-system-native-frame-test PROPERTIES AUTOMOC ON)
    add_test(NAME window-system.native-frame COMMAND window-system-native-frame-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/native-frame-results.txt,txt)
    set_tests_properties(window-system.native-frame PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
