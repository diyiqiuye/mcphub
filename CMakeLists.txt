cmake_minimum_required(VERSION 3.21)
project(mcphub VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_library(mcphub_shared STATIC
    src/shared/protocol.cpp
)
target_include_directories(mcphub_shared PUBLIC src)
target_link_libraries(mcphub_shared PUBLIC Qt6::Core Qt6::Network)

add_executable(mcphubd
    src/daemon/main.cpp
    src/daemon/catalog.cpp
    src/daemon/ipcserver.cpp
    src/daemon/configgen.cpp
    src/daemon/gateway.cpp
    src/daemon/adbprovider.cpp
    src/daemon/provisioner.cpp
    src/daemon/upstreamproxy.cpp
)
target_link_libraries(mcphubd PRIVATE mcphub_shared Qt6::Core Qt6::Network)

set(GUI_SOURCES
    src/gui/main.cpp
    src/gui/mainwindow.cpp
    src/gui/daemonclient.cpp
    src/gui/statuspanel.cpp
    src/gui/topologyview.cpp
    src/gui/logconsole.cpp
    src/gui/titlebar.cpp
    src/gui/widgets/nodestatusindicator.cpp
    src/gui/widgets/dataflowline.cpp
)
add_executable(mcphub WIN32 ${GUI_SOURCES})
target_link_libraries(mcphub PRIVATE mcphub_shared Qt6::Widgets Qt6::Network)

set_target_properties(mcphub PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(mcphubd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
