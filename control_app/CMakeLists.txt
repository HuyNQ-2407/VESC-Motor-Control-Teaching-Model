cmake_minimum_required(VERSION 3.16)
project(VESCController VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network SerialPort Charts)

if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

add_executable(VESC_AML
    main.cpp
    mainwindow.cpp
    mainwindow.h
    protocol.cpp
    protocol.h
    resource.qrc
    ${APP_ICON_RESOURCE_WINDOWS}
)

target_link_libraries(VESC_AML
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::SerialPort
    Qt6::Charts
)
