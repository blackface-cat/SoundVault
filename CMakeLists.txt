cmake_minimum_required(VERSION 3.21)
project(ShengKu VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt 6 Widgets + Svg + Sql + Concurrent + Network（Agent HTTP 接口用 QTcpServer）
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Sql Concurrent Network)

add_executable(ShengKu WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/resources.qrc

    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/WaveformWidget.h
    src/ui/WaveformWidget.cpp
    src/ui/FileListPanel.h
    src/ui/FileListPanel.cpp
    src/ui/DetailPanel.h
    src/ui/DetailPanel.cpp
    src/ui/PlayerPanel.h
    src/ui/PlayerPanel.cpp
    src/ui/Icons.h
    src/ui/Icons.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp

    src/agent/AgentService.h
    src/agent/AgentService.cpp
    src/agent/AgentHttpServer.h
    src/agent/AgentHttpServer.cpp

    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/audio/MiniAudioImpl.cpp
    src/audio/WaveformAnalyzer.h
    src/audio/WaveformAnalyzer.cpp
    src/db/MetadataStore.h
    src/db/MetadataStore.cpp
)

# Windows 资源：exe 内嵌图标（资源管理器/任务栏/快捷方式）
if(WIN32)
    target_sources(ShengKu PRIVATE src/ShengKu.rc)
endif()

target_include_directories(ShengKu PRIVATE src third_party)
target_link_libraries(ShengKu PRIVATE Qt6::Widgets Qt6::Svg Qt6::Sql Qt6::Concurrent Qt6::Network)

if(WIN32)
    # miniaudio 在 MinGW 下需要这些系统库
    target_link_libraries(ShengKu PRIVATE winmm ole32)
endif()

if(APPLE)
    set_target_properties(ShengKu PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "cn.shengku.app"
        MACOSX_BUNDLE_BUNDLE_NAME "声库"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()

# 播放链路真实测试（控制台）：cmake -DBUILD_PLAYTEST=ON
if(BUILD_PLAYTEST)
    add_executable(PlayTest tests/playtest.cpp
        src/audio/AudioEngine.h src/audio/AudioEngine.cpp
        src/audio/MiniAudioImpl.cpp
        src/audio/WaveformAnalyzer.h src/audio/WaveformAnalyzer.cpp
        src/db/MetadataStore.h src/db/MetadataStore.cpp)
    target_include_directories(PlayTest PRIVATE src third_party)
    target_link_libraries(PlayTest PRIVATE Qt6::Core Qt6::Sql)
    if(WIN32)
        target_link_libraries(PlayTest PRIVATE winmm ole32)
    endif()
endif()
