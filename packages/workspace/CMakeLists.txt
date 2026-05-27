find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(workspace STATIC
    include/ComponentItem.h
    include/ConnectionCable.h
    include/WorkspaceScene.h
    include/WorkspaceView.h
    include/CustomComponent.h
    include/ComponentCreatorDialog.h
    src/WorkspaceScene.cpp
    src/WorkspaceView.cpp
    src/ComponentItem.cpp
    src/ConnectionCable.cpp
    src/CustomComponent.cpp
    src/ComponentCreatorDialog.cpp
    src/SmdWizardDialog.cpp
    include/SmdWizardDialog.h
    include/UndoCommands.h
    src/UndoCommands.cpp
)

target_include_directories(workspace PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(workspace PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

