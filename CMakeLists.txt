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
    # catalogs. The check is a PowerShell 7 script, which runs on each
    # platform Qt supports, and uses this build's own lupdate.
    find_package(Qt6 6.9 REQUIRED COMPONENTS LinguistTools)
    find_program(MUXORA_PWSH NAMES pwsh REQUIRED)
    add_test(NAME translations.coverage
        COMMAND "${MUXORA_PWSH}" -NoProfile -File "${PROJECT_SOURCE_DIR}/tools/check-translations.ps1"
            -Lupdate "$<TARGET_FILE:Qt6::lupdate>")
    set_tests_properties(translations.coverage PROPERTIES TIMEOUT 120 LABELS model)
endif()
