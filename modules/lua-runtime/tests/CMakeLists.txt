qt_add_executable(lua-runtime-test script_runtime_test.cpp)
target_link_libraries(lua-runtime-test PRIVATE LuaRuntime::Runtime Qt6::Test Muxora::Lua)
target_compile_features(lua-runtime-test PRIVATE cxx_std_20)
set_target_properties(lua-runtime-test PROPERTIES AUTOMOC ON)
add_test(NAME lua-runtime.runtime COMMAND lua-runtime-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/runtime-results.txt,txt)
set_tests_properties(lua-runtime.runtime PROPERTIES TIMEOUT 60 LABELS model)

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
