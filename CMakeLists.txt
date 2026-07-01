cmake_minimum_required(VERSION 3.16)
project(TextChunkerPro VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt packages
find_package(Qt6 COMPONENTS
    Core
    Gui
    Widgets
    REQUIRED
)

# Main executable
add_executable(text_chunker
    src/main.cpp
)

# Link libraries
target_link_libraries(text_chunker
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    # Linux
    target_compile_options(text_chunker PRIVATE -Wall -Wextra)
elseif(APPLE)
    # macOS
    target_compile_options(text_chunker PRIVATE -Wall -Wextra)
    # For Retina display support
    set_target_properties(text_chunker PROPERTIES
        MACOSX_BUNDLE TRUE
    )
elseif(WIN32)
    # Windows
    target_compile_options(text_chunker PRIVATE /W4)
endif()

# Installation
install(TARGETS text_chunker RUNTIME DESTINATION bin)

# Optional: Create desktop entry for Linux
if(UNIX AND NOT APPLE)
    install(FILES text_chunker.desktop
            DESTINATION share/applications)
endif()

# Print configuration
message(STATUS "Text Chunker Pro Enhanced - Build Configuration")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt Version: ${Qt6_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
