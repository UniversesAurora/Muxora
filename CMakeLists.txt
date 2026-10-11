cmake_minimum_required(VERSION 3.25)
project(Muxora VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets)
add_subdirectory(modules/window-system)
add_subdirectory(src/app)
