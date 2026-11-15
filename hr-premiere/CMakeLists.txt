cmake_minimum_required(VERSION 3.21)
project(HRPremiere VERSION 0.1.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)   # the application icon lives in a .rc file
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Svg Network Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)

if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)
endif()

# ---------------------------------------------------------------- core / engine
# Everything that does not depend on widgets: the project model, the editing
# operations, the FFmpeg decoders, the frame compositor, the audio mixer and the
# exporter.  The test-suite links against this library directly.
add_library(hrcore STATIC
    src/core/Effects.cpp
    src/core/Model.cpp
    src/core/EditOps.cpp
    src/core/Project.cpp
    src/core/Util.cpp
    src/media/FFmpegUtil.cpp
    src/media/MediaProbe.cpp
    src/media/VideoDecoder.cpp
    src/media/AudioDecoder.cpp
    src/media/MediaAssets.cpp
    src/engine/DecoderPool.cpp
    src/engine/PixelEffects.cpp
    src/engine/Renderer.cpp
    src/engine/AudioMixer.cpp
    src/engine/FrameServer.cpp
    src/engine/AudioOutput.cpp
    src/engine/Player.cpp
    src/engine/Exporter.cpp
    src/engine/VoiceRecorder.cpp
    src/ai/Silence.cpp
    src/ai/Scenes.cpp
    src/ai/Transcript.cpp
    src/ai/Whisper.cpp
    src/ai/TranscribeCli.cpp
)
target_include_directories(hrcore PUBLIC src)
target_link_libraries(hrcore PUBLIC Qt6::Widgets Qt6::Multimedia Qt6::Svg Qt6::Network PkgConfig::FFMPEG)

# ---------------------------------------------------------------- application
option(HR_BUILD_APP "Build the HRPremiere application (needs the UI sources)" ON)
if(HR_BUILD_APP)
    add_library(hrui OBJECT
        src/ui/Theme.cpp
        src/ui/Icons.cpp
        src/ui/ScrubLabel.cpp
        src/ui/MonitorPanel.cpp
        src/ui/ProjectPanel.cpp
        src/ui/EffectControlsPanel.cpp
        src/ui/EffectsBrowser.cpp
        src/ui/TimelineWidget.cpp
        src/ui/ToolsBar.cpp
        src/ui/AudioMeters.cpp
        src/ui/WorkspaceBar.cpp
        src/ui/Dialogs.cpp
        src/ui/AiPanel.cpp
        src/ui/MainWindow.cpp
        resources/resources.qrc
    )
    target_link_libraries(hrui PUBLIC hrcore)

    set(HR_APP_SOURCES src/main.cpp)
    if(WIN32)
        list(APPEND HR_APP_SOURCES resources/hrpremiere.rc)
    endif()
    add_executable(HRPremiere WIN32 ${HR_APP_SOURCES})
    target_link_libraries(HRPremiere PRIVATE hrui)
    set_target_properties(HRPremiere PROPERTIES OUTPUT_NAME "HRPremiere")
endif()

# ---------------------------------------------------------------- tests
enable_testing()
add_executable(hr_tests tests/test_main.cpp)
target_link_libraries(hr_tests PRIVATE hrcore Qt6::Test)
add_executable(fake_ffmpeg tests/fake_ffmpeg.cpp)   # stand-in for ffmpeg in the speech-tool tests
add_dependencies(hr_tests fake_ffmpeg)
add_test(NAME hr_tests COMMAND hr_tests)
set_tests_properties(hr_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(HR_BUILD_APP)
    add_executable(hr_ui_tests tests/test_ui.cpp)
    target_link_libraries(hr_ui_tests PRIVATE hrui Qt6::Test)
    add_dependencies(hr_ui_tests fake_ffmpeg)
    add_test(NAME hr_ui_tests COMMAND hr_ui_tests)
    set_tests_properties(hr_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(HR_BUILD_APP)
    install(TARGETS HRPremiere RUNTIME DESTINATION bin)
endif()
