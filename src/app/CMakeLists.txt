qt_add_executable(muxora WIN32 main.cpp state_paths.cpp user_preferences.cpp
    scripting_setup.cpp scripting_setup.hpp script_services.cpp script_services.hpp
    ../workspace_host/placeholder_content.cpp)
qt_add_lrelease(TS_FILES translations/muxora_en.ts translations/muxora_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE muxora_translation_files LRELEASE_TARGET muxora_lrelease OPTIONS -nounfinished)
qt_add_resources(muxora muxora_translations PREFIX "/muxora/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${muxora_translation_files})
qt_add_lupdate(SOURCE_TARGETS muxora TS_FILES translations/muxora_en.ts translations/muxora_zh_CN.ts
    LUPDATE_TARGET muxora_lupdate OPTIONS -no-obsolete)
target_link_libraries(muxora PRIVATE WindowSystemLua::Host)
if(WIN32)
    target_link_libraries(muxora PRIVATE winmm)
endif()
target_compile_features(muxora PRIVATE cxx_std_20)
target_include_directories(muxora PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(muxora PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(muxora PROPERTIES AUTOMOC ON)
if(MSVC)
    target_compile_options(muxora PRIVATE /utf-8)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
    qt_add_executable(muxora-localization-test tests/localization_test.cpp ../workspace_host/placeholder_content.cpp)
    target_link_libraries(muxora-localization-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_compile_features(muxora-localization-test PRIVATE cxx_std_20)
    target_include_directories(muxora-localization-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set_target_properties(muxora-localization-test PROPERTIES AUTOMOC ON)
    qt_add_resources(muxora-localization-test muxora_test_translations PREFIX "/muxora/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${muxora_translation_files})
    add_test(NAME muxora.localization COMMAND muxora-localization-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/localization-results.txt,txt)
    set_tests_properties(muxora.localization PROPERTIES LABELS widget)
    qt_add_executable(muxora-state-paths-test tests/state_paths_test.cpp state_paths.cpp)
    target_link_libraries(muxora-state-paths-test PRIVATE Qt6::Core Qt6::Test)
    target_compile_features(muxora-state-paths-test PRIVATE cxx_std_20)
    target_include_directories(muxora-state-paths-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(muxora-state-paths-test PROPERTIES AUTOMOC ON)
    add_test(NAME muxora.state-paths COMMAND muxora-state-paths-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/state-paths-results.txt,txt)
    set_tests_properties(muxora.state-paths PROPERTIES LABELS model)

    qt_add_executable(muxora-user-preferences-test tests/user_preferences_test.cpp user_preferences.cpp)
    target_link_libraries(muxora-user-preferences-test PRIVATE WindowSystem::Widgets Qt6::Test)
    target_compile_features(muxora-user-preferences-test PRIVATE cxx_std_20)
    target_include_directories(muxora-user-preferences-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(muxora-user-preferences-test PROPERTIES AUTOMOC ON)
    add_test(NAME muxora.user-preferences COMMAND muxora-user-preferences-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/user-preferences-results.txt,txt)
    set_tests_properties(muxora.user-preferences PROPERTIES LABELS model)

    qt_add_executable(muxora-script-child tests/script_child.cpp)
    target_link_libraries(muxora-script-child PRIVATE Qt6::Core)
    target_compile_features(muxora-script-child PRIVATE cxx_std_20)
    qt_add_executable(muxora-scripting-test tests/scripting_test.cpp
        scripting_setup.cpp scripting_setup.hpp script_services.cpp script_services.hpp user_preferences.cpp)
    target_link_libraries(muxora-scripting-test PRIVATE WindowSystemLua::Host Qt6::Test)
    if(WIN32)
        target_link_libraries(muxora-scripting-test PRIVATE winmm)
    endif()
    target_compile_features(muxora-scripting-test PRIVATE cxx_std_20)
    target_include_directories(muxora-scripting-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(muxora-scripting-test PROPERTIES AUTOMOC ON)
    add_dependencies(muxora-scripting-test muxora-script-child)
    add_test(NAME muxora.scripting COMMAND muxora-scripting-test
        -o ${CMAKE_CURRENT_BINARY_DIR}/scripting-results.txt,txt)
    set_tests_properties(muxora.scripting PROPERTIES TIMEOUT 60 LABELS widget)
endif()
