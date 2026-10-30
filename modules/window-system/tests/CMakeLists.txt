qt_add_executable(window-system-model-test layout_model_test.cpp)
target_link_libraries(window-system-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.model COMMAND window-system-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/model-results.txt,txt)
set_tests_properties(window-system.model PROPERTIES LABELS model)

# One test executable per suite: <suite>_test.cpp, its label from
# docs/development/testing-design.md (desktop suites run serially). LINK
# defaults to the Widgets library; SOURCES adds sources compiled into it.
function(window_system_add_suite suite label)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "LINK;SOURCES")
    if(NOT arg_LINK)
        set(arg_LINK WindowSystem::Widgets)
    endif()
    string(REPLACE "-" "_" source_name ${suite})
    qt_add_executable(window-system-${suite}-test ${source_name}_test.cpp ${arg_SOURCES})
    target_link_libraries(window-system-${suite}-test PRIVATE ${arg_LINK} Qt6::Test)
    target_compile_features(window-system-${suite}-test PRIVATE cxx_std_20)
    target_include_directories(window-system-${suite}-test PRIVATE ../src ../include)
    set_target_properties(window-system-${suite}-test PROPERTIES AUTOMOC ON)
    add_test(NAME window-system.${suite} COMMAND window-system-${suite}-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/${suite}-results.txt,txt)
    set_tests_properties(window-system.${suite} PROPERTIES TIMEOUT 60 LABELS ${label})
    if(label STREQUAL "desktop")
        set_tests_properties(window-system.${suite} PROPERTIES RUN_SERIAL TRUE)
    endif()
endfunction()

foreach(suite IN ITEMS frame-motion tab-position-motion label-presence tab-label-geometry tab-reorder
        tab-pointer-controller tab-hover-controller tab-keyboard-controller tab-wheel-controller tab-drag-geometry)
    window_system_add_suite(${suite} model LINK WindowSystemPresentation)
endforeach()
window_system_add_suite(tab-viewport-reflow model LINK WindowSystem::Model)
window_system_add_suite(dock-guides-geometry model LINK WindowSystem::Model)
window_system_add_suite(quick-path model LINK Qt6::Gui
    SOURCES ../src/quick/path_geometry.cpp ../src/docking/surface_geometry.cpp)
# The surface geometry, renderer and shared raster resources link without
# Widgets, just as the Quick adapter prepares GUI-thread resources.
foreach(suite IN ITEMS surface-elevation tab-carrier-presentation tab-label-store tab-strip-arrangement)
    window_system_add_suite(${suite} widget LINK WindowSystemSurfaces)
endforeach()

if(TARGET WindowSystemQuick)
    foreach(suite IN ITEMS quick-tab-bar-input quick-text-resources quick-label-resources quick-carrier-resources
            quick-foreground-resources quick-strip-resources quick-rendering quick-group)
        window_system_add_suite(${suite} desktop LINK WindowSystemQuick)
    endforeach()
    window_system_add_suite(quick-content-host desktop LINK WindowSystemQuick WindowSystemPlatform WindowSystem::Model)
    window_system_add_suite(quick-split-tree desktop LINK WindowSystemQuick WindowSystem::Model)
    window_system_add_suite(quick-tab-presentation desktop LINK WindowSystemQuick WindowSystem::Widgets)
    if(WIN32)
        window_system_add_suite(quick-native-frame desktop LINK WindowSystemQuick WindowSystemPlatform user32)
    endif()
endif()

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
set(window_system_model_suites pinned-layout pinned-positions tab-layout)
set(window_system_widget_suites smooth-scroll surface-lifecycle surface-bar mixed-tab-rows hierarchy-conversion visibility-profile tab-appearance tab-title-layout shortcut-settings localization input-method-scope
    interface-appearance tab-label-presentation tab-carrier-source tab-presentation-source tab-reorder-wiring edge-reorder)
set(window_system_desktop_suites surface-transfer pane-settings pane-bar-policy pane-minimum pinned-ui window-placement tab-bar-presentation edge-tab-interaction chrome-interaction caption-strip toolbar-popup toolbar-reset toolbar-policy sidebar-menu sidebar-presentation presentation compact-ui movement edge-strip drag-presentation strip-drag menu-structure view-state focus-mode multirow-toolbar view-commands peek-size toolbar-profile tab-viewport floating-panel status-bar tab-creation tab-strip-layout sticky-tabs popup-menu tab-group shortcut-dispatch memory-lifecycle view-reuse
    integer-slider input-method-native)
if(WIN32)
    list(APPEND window_system_desktop_suites native-drag)
endif()

foreach(label model widget desktop)
    foreach(suite IN LISTS window_system_${label}_suites)
        window_system_add_suite(${suite} ${label})
    endforeach()
endforeach()
# Screen-edge pixels at a second, fractional scale: QT_SCALE_FACTOR=0.75 turns
# a 200 % monitor into 1.5 (the last physical column maps to x.33 instead of
# x.5), as on a 150 % laptop panel.
add_test(NAME window-system.caption-strip-scaled COMMAND window-system-caption-strip-test
    expandedCaptionControlsReachEveryScreenCorner maximizedCaptionControlsOwnTheActualScreenEdge
    -o ${CMAKE_CURRENT_BINARY_DIR}/caption-strip-scaled-results.txt,txt)
set_tests_properties(window-system.caption-strip-scaled PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE LABELS desktop
    ENVIRONMENT QT_SCALE_FACTOR=0.75)
qt_add_executable(window-system-tab-ordering-model-test tab_ordering_model_test.cpp)
target_link_libraries(window-system-tab-ordering-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-tab-ordering-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.tab-ordering-model COMMAND window-system-tab-ordering-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/tab-ordering-model-results.txt,txt)
set_tests_properties(window-system.tab-ordering-model PROPERTIES LABELS model)

qt_add_executable(window-system-tab-folder-model-test tab_folder_model_test.cpp)
target_link_libraries(window-system-tab-folder-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-tab-folder-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.tab-folder-model COMMAND window-system-tab-folder-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/tab-folder-model-results.txt,txt)
set_tests_properties(window-system.tab-folder-model PROPERTIES LABELS model)

qt_add_executable(window-system-movement-model-test movement_model_test.cpp)
target_link_libraries(window-system-movement-model-test PRIVATE WindowSystem::Model Qt6::Test)
set_target_properties(window-system-movement-model-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.movement-model COMMAND window-system-movement-model-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/movement-model-results.txt,txt)
set_tests_properties(window-system.movement-model PROPERTIES LABELS model)

if(WIN32)
    target_link_libraries(window-system-memory-lifecycle-test PRIVATE psapi user32)
    target_link_libraries(window-system-input-method-native-test PRIVATE user32 imm32 ole32 uuid)
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

# Suite-total hang guards for measured MSVC Debug workloads, not product
# performance targets. See docs/development/testing-design.md, CTest limits.
if(MSVC)
    set_tests_properties(
        window-system.custom-chrome window-system.pane-settings window-system.caption-strip
        window-system.drag-presentation window-system.view-state
        window-system.tab-viewport window-system.tab-group
        window-system.memory-lifecycle
        PROPERTIES TIMEOUT "$<IF:$<CONFIG:Debug>,300,60>")
    if(TEST window-system.native-frame)
        set_tests_properties(window-system.native-frame
            PROPERTIES TIMEOUT "$<IF:$<CONFIG:Debug>,300,60>")
    endif()
    set_tests_properties(window-system.strip-drag
        PROPERTIES TIMEOUT "$<IF:$<CONFIG:Debug>,720,120>")
endif()

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

# MSVC reads sources in the system code page unless told otherwise; these
# sources are UTF-8 (Chinese interface text and test data).
if(MSVC)
    get_property(_utf8_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_utf8_target IN LISTS _utf8_targets)
        get_target_property(_utf8_type ${_utf8_target} TYPE)
        if(_utf8_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            target_compile_options(${_utf8_target} PRIVATE /utf-8)
        endif()
    endforeach()
endif()
