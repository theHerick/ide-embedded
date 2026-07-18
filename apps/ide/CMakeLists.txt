find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets SerialPort Network OPTIONAL_COMPONENTS Pdf PdfWidgets)

add_executable(ide-embedded WIN32
    include/MainWindow.h
    include/OscilloscopePanel.h
    include/WebPageEditorDialog.h
    include/WebElementItem.h
    include/TutorialOverlay.h
    main.cpp
    src/MainWindow.cpp
    src/ProjectManager.cpp
    src/OscilloscopePanel.cpp
    src/WebPageEditorDialog.cpp
    src/WebElementItem.cpp
    src/TutorialOverlay.cpp
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
    Qt6::Network
    workspace
    block_engine
    compiler
    simulator
)

if (TARGET Qt6::Pdf)
    target_link_libraries(ide-embedded PRIVATE Qt6::Pdf)
    target_compile_definitions(ide-embedded PRIVATE IDE_EMBEDDED_HAS_QT_PDF)
endif()

if (TARGET Qt6::PdfWidgets)
    target_link_libraries(ide-embedded PRIVATE Qt6::PdfWidgets)
endif()
