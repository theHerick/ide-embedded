cmake_minimum_required(VERSION 3.16)
project(IDEEmbedded VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build type for optimal speed and size
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Enable automatic compilation of Qt files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Optimization compiler and linker options (LTO, dead code elimination, max optimization)
if(MSVC)
    add_compile_options(/O2 /GL /Gw)
    add_link_options(/LTCG /OPT:REF /OPT:ICF)
else()
    # For MinGW/GCC C++ (using -O3, Link-Time Optimization, and section garbage collection)
    add_compile_options(-O3 -flto -ffunction-sections -fdata-sections)
    add_link_options(-flto -Wl,--gc-sections)
endif()

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Qml)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/workspace/include
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/block-engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/compiler/include
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/simulator/include
)

# Subdirectories
add_subdirectory(packages/workspace)
add_subdirectory(packages/block-engine)
add_subdirectory(packages/compiler)
add_subdirectory(packages/simulator)
add_subdirectory(apps/ide)
