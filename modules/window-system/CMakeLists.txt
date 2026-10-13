cmake_minimum_required(VERSION 3.25)
project(WindowSystem VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
option(WINDOW_SYSTEM_BUILD_TESTS "Build WindowSystem automated tests" ${BUILD_TESTING})

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets)

add_library(WindowSystemModel STATIC src/model/layout_model.cpp)
add_library(WindowSystem::Model ALIAS WindowSystemModel)
set_target_properties(WindowSystemModel PROPERTIES EXPORT_NAME Model)
target_compile_features(WindowSystemModel PUBLIC cxx_std_20)
target_include_directories(WindowSystemModel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(WindowSystemModel PUBLIC Qt6::Core)

add_library(WindowSystemWidgets STATIC
    src/window_manager.cpp
    src/navigation/workspace_navigation.cpp
    src/layout_persistence.cpp
    src/chrome/workspace_window.cpp
    src/chrome/command_strip.cpp
    src/commands/command_registry.cpp
    src/commands/window_commands.cpp
    src/commands/command_dialogs.cpp
    src/docking/dock_surface.cpp
    src/docking/tab_strip.cpp
    src/docking/edge_strip.cpp
    src/docking/drag_presentation.cpp
    src/docking/edge_tab_button.cpp
    src/docking/drag_controller.cpp
    src/panes/tab_content.cpp
    src/style/theme.cpp
    src/style/themed_controls.cpp
    src/settings/settings_page.cpp
    include/window_system/window_manager.hpp)
add_library(WindowSystem::Widgets ALIAS WindowSystemWidgets)
set_target_properties(WindowSystemWidgets PROPERTIES EXPORT_NAME Widgets AUTOMOC ON)
target_compile_features(WindowSystemWidgets PUBLIC cxx_std_20)
target_include_directories(WindowSystemWidgets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(WindowSystemWidgets PUBLIC WindowSystem::Model Qt6::Widgets)
if(WIN32)
    target_sources(WindowSystemWidgets PRIVATE src/platform/windows/window_frame.cpp)
    target_link_libraries(WindowSystemWidgets PRIVATE dwmapi user32)
endif()
foreach(target WindowSystemModel WindowSystemWidgets)
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

install(TARGETS WindowSystemModel WindowSystemWidgets EXPORT WindowSystemTargets
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
