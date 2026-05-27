find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(compiler STATIC
    src/CodeGenerator.cpp
    src/PcbExporter.cpp
)

target_include_directories(compiler PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(compiler PUBLIC
    Qt6::Core
    workspace
    block_engine
)
