find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(block_engine STATIC
    include/BlockEditor.h
    include/VariableSystem.h
    src/BlockEditor.cpp
    src/VariableSystem.cpp
)

target_include_directories(block_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(block_engine PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
