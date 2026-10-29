cmake_minimum_required(VERSION 3.25)
project(WindowSystem VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
option(WINDOW_SYSTEM_BUILD_TESTS "Build WindowSystem automated tests" ${BUILD_TESTING})
option(WINDOW_SYSTEM_BUILD_BENCHMARKS "Build WindowSystem scale benchmarks" OFF)
option(WINDOW_SYSTEM_BUILD_QUICK "Build the in-progress Quick rendering layer" OFF)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)

add_library(WindowSystemModel STATIC src/model/layout_model.cpp src/model/tab_ordering.cpp
    src/model/pinned_layout.cpp src/model/pinned_positions.cpp src/model/hierarchy_conversion.cpp)
add_library(WindowSystem::Model ALIAS WindowSystemModel)
set_target_properties(WindowSystemModel PROPERTIES EXPORT_NAME Model)
target_compile_features(WindowSystemModel PUBLIC cxx_std_20)
target_include_directories(WindowSystemModel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(WindowSystemModel PUBLIC Qt6::Core)

# Presentation state and layout are shared by both rendering adapters. This
# target has no widget, scene graph or platform-window dependency.
add_library(WindowSystemPresentation STATIC
    src/docking/tab_layout.cpp src/docking/tab_wheel_controller.cpp
    src/docking/tab_drag_geometry.cpp src/style/frame_motion.cpp src/style/scroll_motion_state.cpp)
set_target_properties(WindowSystemPresentation PROPERTIES EXPORT_NAME Presentation)
target_compile_features(WindowSystemPresentation PUBLIC cxx_std_20)
target_include_directories(WindowSystemPresentation PRIVATE src)
target_link_libraries(WindowSystemPresentation PRIVATE Qt6::Core)

# GUI values and bounded surface resources shared by both drawing adapters.
add_library(WindowSystemSurfaces STATIC
    src/style/elevation.cpp src/docking/surface_geometry.cpp
    src/style/tab_painter.cpp src/style/tab_title_layout.cpp src/style/tab_carrier_painter.cpp
    src/style/theme_resources.cpp src/style/tab_departure.cpp
    src/docking/tab_incoming_state.cpp src/docking/tab_label_store.cpp
    src/docking/tab_strip_arrangement.cpp src/docking/tab_group_geometry.cpp
    src/docking/tab_group_surface.cpp)
set_target_properties(WindowSystemSurfaces PROPERTIES EXPORT_NAME Surfaces)
target_compile_features(WindowSystemSurfaces PUBLIC cxx_std_20)
target_include_directories(WindowSystemSurfaces PRIVATE src include)
target_link_libraries(WindowSystemSurfaces PUBLIC Qt6::Gui PRIVATE WindowSystemPresentation)

add_library(WindowSystemWidgets STATIC
    src/localization/localization.cpp
    src/window_manager.cpp
    src/input_method_scopes.cpp
    src/platform/input_method_state.cpp
    src/view_state.cpp
    src/navigation/workspace_navigation.cpp
    src/layout_persistence.cpp
    src/chrome/workspace_window.cpp
    src/chrome/status_bar.cpp
    src/chrome/workspace_view.cpp
    src/chrome/command_strip.cpp
    src/chrome/toolbar_api.cpp
    src/custom_chrome.cpp
    src/floating_panel.cpp
    src/commands/command_registry.cpp
    src/commands/command_menus.cpp
    src/commands/popup_menu.cpp
    src/commands/tab_group_commands.cpp
    src/commands/tab_folder_commands.cpp
    src/tab_group_api.cpp
    src/tab_folder_api.cpp
    src/hierarchy_api.cpp
    src/commands/hierarchy_commands.cpp
    src/commands/window_commands.cpp
    src/commands/interface_appearance_commands.cpp
    src/commands/tab_presentation_commands.cpp
    src/commands/toolbar_move_command.cpp
    src/commands/peek_size_command.cpp
    src/commands/command_dialogs.cpp
    src/docking/dock_surface.cpp
    src/docking/tab_strip.cpp
    src/docking/tab_bar.cpp
    src/docking/tab_strip_frame.cpp
    src/docking/tab_foreground_frame.cpp
    src/docking/edge_strip.cpp
    src/docking/drag_presentation.cpp
    src/docking/edge_tab_button.cpp
    src/docking/drag_controller.cpp
    src/panes/tab_content.cpp
    src/style/theme.cpp
    src/style/interface_appearance.cpp
    src/style/tab_appearance.cpp
    src/style/themed_controls.cpp
    src/style/integer_slider.cpp
    src/style/wheel_guard.cpp
    src/style/smooth_scroll.cpp
    src/style/tooltip_layout.cpp
    src/settings/settings_page.cpp
    src/settings/settings_interface_appearance.cpp
    src/settings/shortcuts_page.cpp
    src/settings/settings_view_options.cpp
    src/settings/settings_tab_appearance.cpp
    include/window_system/window_manager.hpp
    include/window_system/localization.hpp
    include/window_system/interface_appearance.hpp
    include/window_system/floating_panel.hpp)
add_library(WindowSystem::Widgets ALIAS WindowSystemWidgets)
set_target_properties(WindowSystemWidgets PROPERTIES EXPORT_NAME Widgets AUTOMOC ON)
target_compile_features(WindowSystemWidgets PUBLIC cxx_std_20)
target_include_directories(WindowSystemWidgets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(WindowSystemWidgets PUBLIC WindowSystem::Model Qt6::Widgets PRIVATE WindowSystemPresentation WindowSystemSurfaces)

if(WINDOW_SYSTEM_BUILD_QUICK)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
    # Internal until content and native-window interfaces complete the
    # migration. Enabling this does not replace the application renderer.
    add_library(WindowSystemQuick STATIC
        src/quick/path_geometry.cpp src/quick/path_item.cpp src/quick/window_motion.cpp
        src/quick/body_shadow_item.cpp src/quick/anchor_shadow_item.cpp
        src/quick/group_surface_item.cpp src/quick/tab_label_text_item.cpp
        src/quick/tab_icon_item.cpp src/quick/tab_label_item.cpp
        src/quick/tab_strip_item.cpp)
    set_target_properties(WindowSystemQuick PROPERTIES AUTOMOC ON)
    target_compile_features(WindowSystemQuick PUBLIC cxx_std_20)
    target_include_directories(WindowSystemQuick PUBLIC include PRIVATE src)
    target_link_libraries(WindowSystemQuick PUBLIC Qt6::Quick PRIVATE WindowSystemPresentation WindowSystemSurfaces)
endif()

# Keep editable Qt Linguist sources in version control; compiled catalogs are
# generated and embedded for SDK-independent deployment and module consumers.
set(window_system_translation_sources
    translations/window_system_en.ts translations/window_system_zh_CN.ts)
qt_add_lrelease(TS_FILES ${window_system_translation_sources}
    QM_FILES_OUTPUT_VARIABLE window_system_translation_files
    LRELEASE_TARGET window_system_lrelease OPTIONS -nounfinished)
get_target_property(window_system_qtpaths Qt6::qtpaths IMPORTED_LOCATION)
execute_process(COMMAND "${window_system_qtpaths}" --query QT_INSTALL_TRANSLATIONS
    OUTPUT_VARIABLE window_system_qt_translations_dir OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE window_system_qtpaths_result)
if(NOT window_system_qtpaths_result EQUAL 0)
    message(FATAL_ERROR "Could not locate the installed Qt translations using qtpaths")
endif()
find_file(window_system_qt_zh_catalog qtbase_zh_CN.qm
    PATHS "${window_system_qt_translations_dir}" NO_DEFAULT_PATH REQUIRED)
# The source-file resource form puts rcc output directly into this static
# archive. Installed consumers need no separate object/resource export targets.
set(window_system_catalog_qrc_entries "")
foreach(catalog IN LISTS window_system_translation_files)
    get_filename_component(catalog_name "${catalog}" NAME)
    string(REPLACE "&" "&amp;" catalog_qrc_path "${catalog}")
    string(REPLACE "<" "&lt;" catalog_qrc_path "${catalog_qrc_path}")
    string(APPEND window_system_catalog_qrc_entries "        <file alias=\"${catalog_name}\">${catalog_qrc_path}</file>\n")
endforeach()
string(REPLACE "&" "&amp;" window_system_qt_zh_catalog_qrc_path "${window_system_qt_zh_catalog}")
string(REPLACE "<" "&lt;" window_system_qt_zh_catalog_qrc_path "${window_system_qt_zh_catalog_qrc_path}")
configure_file(cmake/window_system_translations.qrc.in
    "${CMAKE_CURRENT_BINARY_DIR}/window_system_translations.qrc" @ONLY)
qt_add_resources(window_system_translation_resource
    "${CMAKE_CURRENT_BINARY_DIR}/window_system_translations.qrc")
target_sources(WindowSystemWidgets PRIVATE ${window_system_translation_resource})
add_dependencies(WindowSystemWidgets window_system_lrelease)
set(QT_I18N_SOURCE_LANGUAGE en)
# Extract public defaults, model errors, and the Lua adapter's diagnostics as
# well as widget text. LuaRuntime keeps its own catalog.
file(GLOB_RECURSE window_system_i18n_sources CONFIGURE_DEPENDS
    "include/*.hpp" "src/*.hpp" "src/*.cpp"
    "../window-system-lua/include/*.hpp" "../window-system-lua/src/*.hpp" "../window-system-lua/src/*.cpp")
qt_add_lupdate(SOURCE_TARGETS WindowSystemWidgets WindowSystemModel
    SOURCES ${window_system_i18n_sources}
    TS_FILES ${window_system_translation_sources}
    LUPDATE_TARGET window_system_lupdate OPTIONS -no-obsolete -locations none -source-language en)
if(WIN32)
    target_sources(WindowSystemWidgets PRIVATE src/platform/windows/window_frame.cpp src/platform/windows/window_menu.cpp src/platform/windows/font_families.cpp)
    target_link_libraries(WindowSystemWidgets PRIVATE dwmapi user32 imm32 ole32 uuid dwrite)
endif()
set(window_system_compile_targets WindowSystemModel WindowSystemPresentation WindowSystemSurfaces WindowSystemWidgets)
if(TARGET WindowSystemQuick)
    list(APPEND window_system_compile_targets WindowSystemQuick)
endif()
foreach(target IN LISTS window_system_compile_targets)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

if(WINDOW_SYSTEM_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
# Benchmarks build in their own directory (tools/bench.ps1) and add no tests.
if(WINDOW_SYSTEM_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

install(TARGETS WindowSystemModel WindowSystemPresentation WindowSystemSurfaces WindowSystemWidgets EXPORT WindowSystemTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/window_system DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT WindowSystemTargets FILE WindowSystemTargets.cmake
    NAMESPACE WindowSystem:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WindowSystem)
configure_package_config_file(cmake/WindowSystemConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WindowSystemConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WindowSystem)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/WindowSystemConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/WindowSystemConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WindowSystemConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WindowSystem)
