qt_add_executable(window-system-lua-test script_host_test.cpp)
target_link_libraries(window-system-lua-test PRIVATE WindowSystemLua::Host Qt6::Test)
set_target_properties(window-system-lua-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.lua COMMAND window-system-lua-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/script-host-results.txt,txt)
set_tests_properties(window-system.lua PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)

qt_add_executable(window-system-lua-lifecycle-test script_lifecycle_test.cpp)
target_link_libraries(window-system-lua-lifecycle-test PRIVATE WindowSystemLua::Host Qt6::Test)
set_target_properties(window-system-lua-lifecycle-test PROPERTIES AUTOMOC ON)
if(WIN32)
    target_link_libraries(window-system-lua-lifecycle-test PRIVATE psapi)
endif()
add_test(NAME window-system.lua-lifecycle COMMAND window-system-lua-lifecycle-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/script-lifecycle-results.txt,txt)
set_tests_properties(window-system.lua-lifecycle PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
