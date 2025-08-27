cmake_minimum_required(VERSION 3.16)
project(textchunker)

set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

qt6_standard_project_setup()

add_executable(textchunker src/gui.cpp)
target_link_libraries(textchunker Qt6::Core Qt6::Widgets Qt6::Gui)
