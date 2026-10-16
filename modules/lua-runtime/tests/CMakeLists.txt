qt_add_executable(lua-runtime-test script_runtime_test.cpp)
target_link_libraries(lua-runtime-test PRIVATE LuaRuntime::Runtime Qt6::Test)
target_compile_features(lua-runtime-test PRIVATE cxx_std_20)
set_target_properties(lua-runtime-test PROPERTIES AUTOMOC ON)
add_test(NAME lua-runtime.runtime COMMAND lua-runtime-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/runtime-results.txt,txt)
set_tests_properties(lua-runtime.runtime PROPERTIES TIMEOUT 60)
