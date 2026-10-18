cmake_minimum_required(VERSION 3.25)
project(WindowSystemLua VERSION 0.1.0 LANGUAGES CXX)
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Widgets)
add_library(WindowSystemLua STATIC
    src/script_host.cpp src/window_api.cpp src/script_api.cpp src/appearance_api.cpp src/hierarchy_api.cpp
    include/window_system_lua/script_host.hpp)
add_library(WindowSystemLua::Host ALIAS WindowSystemLua)
set_target_properties(WindowSystemLua PROPERTIES AUTOMOC ON)
target_compile_features(WindowSystemLua PUBLIC cxx_std_20)
target_include_directories(WindowSystemLua PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(WindowSystemLua PUBLIC WindowSystem::Widgets LuaRuntime::Runtime)
if(BUILD_TESTING)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
