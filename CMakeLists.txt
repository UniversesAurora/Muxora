cmake_minimum_required(VERSION 3.25)
project(Muxora VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets)
add_subdirectory(modules/window-system)
add_subdirectory(modules/lua-runtime)
add_subdirectory(modules/window-system-lua)
add_subdirectory(src/app)

if(BUILD_TESTING)
    # Compares a fresh extraction of every first-party source with the
    # catalogs, using this build's own lupdate, and checks that the checker
    # rejects each broken input it is meant to catch.
    find_package(Qt6 6.9 REQUIRED COMPONENTS LinguistTools)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME translations.coverage
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check-translations.py"
            --lupdate "$<TARGET_FILE:Qt6::lupdate>")
    add_test(NAME translations.checker
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/tests/translations-test.py"
            --lupdate "$<TARGET_FILE:Qt6::lupdate>")
    set_tests_properties(translations.coverage translations.checker PROPERTIES TIMEOUT 300 LABELS model)
endif()
