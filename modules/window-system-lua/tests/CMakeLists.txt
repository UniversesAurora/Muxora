qt_add_executable(window-system-lua-test script_host_test.cpp)
target_link_libraries(window-system-lua-test PRIVATE WindowSystemLua::Host Qt6::Test)
set_target_properties(window-system-lua-test PROPERTIES AUTOMOC ON)
add_test(NAME window-system.lua COMMAND window-system-lua-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/script-host-results.txt,txt)
set_tests_properties(window-system.lua PROPERTIES TIMEOUT 90 LABELS widget)

qt_add_executable(window-system-lua-lifecycle-test script_lifecycle_test.cpp)
target_link_libraries(window-system-lua-lifecycle-test PRIVATE WindowSystemLua::Host Qt6::Test)
set_target_properties(window-system-lua-lifecycle-test PROPERTIES AUTOMOC ON)
if(WIN32)
    target_link_libraries(window-system-lua-lifecycle-test PRIVATE psapi)
endif()
add_test(NAME window-system.lua-lifecycle COMMAND window-system-lua-lifecycle-test
    -o ${CMAKE_CURRENT_BINARY_DIR}/script-lifecycle-results.txt,txt)
# Win32 process counters keep this memory trend suite in the serial desktop layer.
set_tests_properties(window-system.lua-lifecycle PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS desktop)

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
