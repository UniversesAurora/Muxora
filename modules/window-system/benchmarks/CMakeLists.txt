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
