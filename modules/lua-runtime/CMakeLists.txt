cmake_minimum_required(VERSION 3.25)
project(LuaRuntime VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
option(LUA_RUNTIME_BUILD_TESTS "Build Lua runtime automated tests" ${BUILD_TESTING})
find_package(Qt6 6.9 REQUIRED COMPONENTS Core LinguistTools)

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
# The runtime's diagnostics have their own catalog. The source-file resource
# form puts rcc output into this static archive, as WindowSystem does.
set(lua_runtime_translation_sources translations/lua_runtime_en.ts translations/lua_runtime_zh_CN.ts)
qt_add_lrelease(TS_FILES ${lua_runtime_translation_sources}
    QM_FILES_OUTPUT_VARIABLE lua_runtime_translation_files
    LRELEASE_TARGET lua_runtime_lrelease OPTIONS -nounfinished)
set(lua_runtime_catalog_qrc_entries "")
foreach(catalog IN LISTS lua_runtime_translation_files)
    get_filename_component(catalog_name "${catalog}" NAME)
    string(REPLACE "&" "&amp;" catalog_qrc_path "${catalog}")
    string(REPLACE "<" "&lt;" catalog_qrc_path "${catalog_qrc_path}")
    string(APPEND lua_runtime_catalog_qrc_entries "        <file alias=\"${catalog_name}\">${catalog_qrc_path}</file>\n")
endforeach()
configure_file(cmake/lua_runtime_translations.qrc.in
    "${CMAKE_CURRENT_BINARY_DIR}/lua_runtime_translations.qrc" @ONLY)
qt_add_resources(lua_runtime_translation_resource "${CMAKE_CURRENT_BINARY_DIR}/lua_runtime_translations.qrc")
target_sources(LuaRuntime PRIVATE ${lua_runtime_translation_resource})
add_dependencies(LuaRuntime lua_runtime_lrelease)
set(QT_I18N_SOURCE_LANGUAGE en)
qt_add_lupdate(SOURCE_TARGETS LuaRuntime TS_FILES ${lua_runtime_translation_sources}
    LUPDATE_TARGET lua_runtime_lupdate OPTIONS -no-obsolete -locations none -source-language en)

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
