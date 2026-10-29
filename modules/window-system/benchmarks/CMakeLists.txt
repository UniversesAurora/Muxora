# Opt-in scale benchmarks. They print JSON for before/after comparisons and
# never register with CTest.
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(window-system-layout-benchmark layout_scale_benchmark.cpp benchmark_support.cpp)
target_link_libraries(window-system-layout-benchmark PRIVATE WindowSystem::Widgets Qt6::Test)
target_include_directories(window-system-layout-benchmark PRIVATE ../src)
target_compile_features(window-system-layout-benchmark PRIVATE cxx_std_20)
if(WIN32)
    target_link_libraries(window-system-layout-benchmark PRIVATE psapi)
endif()

qt_add_executable(window-system-title-benchmark title_rendering_benchmark.cpp)
target_link_libraries(window-system-title-benchmark PRIVATE WindowSystem::Widgets)
target_include_directories(window-system-title-benchmark PRIVATE ../src)
target_compile_features(window-system-title-benchmark PRIVATE cxx_std_20)

qt_add_executable(window-system-resize-benchmark splitter_resize_benchmark.cpp)
target_link_libraries(window-system-resize-benchmark PRIVATE WindowSystem::Widgets)
target_include_directories(window-system-resize-benchmark PRIVATE ../src)
target_compile_features(window-system-resize-benchmark PRIVATE cxx_std_20)

qt_add_executable(window-system-tab-input-benchmark tab_input_benchmark.cpp)
target_link_libraries(window-system-tab-input-benchmark PRIVATE WindowSystem::Widgets)
target_include_directories(window-system-tab-input-benchmark PRIVATE ../src)
target_compile_features(window-system-tab-input-benchmark PRIVATE cxx_std_20)

if(TARGET WindowSystemQuick)
    qt_add_executable(window-system-quick-frame-benchmark quick_frame_benchmark.cpp tab_frame_scenes.cpp
        ../src/docking/surface_geometry.cpp)
    target_link_libraries(window-system-quick-frame-benchmark PRIVATE WindowSystemQuick WindowSystem::Model)
    target_include_directories(window-system-quick-frame-benchmark PRIVATE ../src)
    target_compile_features(window-system-quick-frame-benchmark PRIVATE cxx_std_20)
endif()

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
