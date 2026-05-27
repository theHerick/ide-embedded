find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(simulator STATIC
    include/HardwareSimulator.h
    src/HardwareSimulator.cpp
)

target_include_directories(simulator PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(simulator PUBLIC
    Qt6::Core
    Qt6::Qml
    workspace
)
