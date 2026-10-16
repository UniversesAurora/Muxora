cmake_minimum_required(VERSION 3.25)
project(LuaRuntime VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
option(LUA_RUNTIME_BUILD_TESTS "Build Lua runtime automated tests" ${BUILD_TESTING})
find_package(Qt6 6.9 REQUIRED COMPONENTS Core)

if(NOT TARGET Muxora::Lua)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/lua"
        "${CMAKE_CURRENT_BINARY_DIR}/third_party/lua")
endif()

add_library(LuaRuntime STATIC
    include/lua_runtime/script_runtime.hpp
    src/script_runtime.cpp
    src/script_worker.cpp
    src/script_worker.hpp
    src/variant_codec.cpp
    src/variant_codec.hpp)
add_library(LuaRuntime::Runtime ALIAS LuaRuntime)
set_target_properties(LuaRuntime PROPERTIES AUTOMOC ON)
target_compile_features(LuaRuntime PUBLIC cxx_std_20)
target_include_directories(LuaRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(LuaRuntime PUBLIC Qt6::Core PRIVATE Muxora::Lua)
if(MSVC)
    target_compile_options(LuaRuntime PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(LuaRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(LUA_RUNTIME_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
