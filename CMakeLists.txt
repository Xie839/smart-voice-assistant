cmake_minimum_required(VERSION 3.20)

project(VoiceFlowAI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "D:/software/Qt/6.8.3/mingw_64/lib/cmake")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

add_executable(VoiceFlowAI
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/TaskConfig.h
)

target_link_libraries(VoiceFlowAI PRIVATE Qt6::Widgets)