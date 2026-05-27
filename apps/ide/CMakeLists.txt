find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets SerialPort Pdf PdfWidgets)

add_executable(ide-embedded
    include/MainWindow.h
    include/OscilloscopePanel.h
    main.cpp
    src/MainWindow.cpp
    src/OscilloscopePanel.cpp
    resources.qrc
    resources.rc
)

target_include_directories(ide-embedded PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ide-embedded PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::Pdf
    Qt6::PdfWidgets
    workspace
    block_engine
    compiler
    simulator
)

