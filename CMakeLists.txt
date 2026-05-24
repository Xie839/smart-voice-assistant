cmake_minimum_required(VERSION 3.20)

project(VoiceFlowAI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VOICEFLOW_QT_CMAKE_DIR "" CACHE PATH "Optional Qt CMake package directory")
if(VOICEFLOW_QT_CMAKE_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${VOICEFLOW_QT_CMAKE_DIR}")
endif()
get_filename_component(DEFAULT_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(MINGW_BIN_DIR "${DEFAULT_MINGW_BIN_DIR}" CACHE PATH "MinGW runtime DLL directory")

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Network)

qt_standard_project_setup()

add_executable(VoiceFlowAI
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/TaskConfig.h
    src/audio/AudioRecorder.h
    src/audio/AudioRecorder.cpp
    src/audio/VadDetector.h
    src/audio/VadDetector.cpp
    src/audio/AudioChunker.h
    src/audio/AudioChunker.cpp
    src/audio/WavWriter.h
    src/audio/WavWriter.cpp
    src/asr/AsrResult.h
    src/asr/TranscriptAssembler.h
    src/asr/TranscriptAssembler.cpp
    src/asr/TextPostProcessor.h
    src/asr/TextPostProcessor.cpp
    src/asr/PunctuationProcessor.h
    src/asr/PunctuationProcessor.cpp
    src/asr/SherpaOnnxAsrEngine.h
    src/asr/SherpaOnnxAsrEngine.cpp
    src/asr/AsrBackendManager.h
    src/asr/AsrBackendManager.cpp
    src/config/AppConfig.h
    src/config/AppConfig.cpp
    src/ai/DeepSeekClient.h
    src/ai/DeepSeekClient.cpp
    src/ai/AiConfigDialog.h
    src/ai/AiConfigDialog.cpp
    src/ai/CustomPromptDialog.h
    src/ai/CustomPromptDialog.cpp
)

target_link_libraries(VoiceFlowAI PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network)

set(MINGW_RUNTIME_DLLS
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll
)
foreach(DLL_NAME ${MINGW_RUNTIME_DLLS})
    if(EXISTS "${MINGW_BIN_DIR}/${DLL_NAME}")
        add_custom_command(TARGET VoiceFlowAI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/${DLL_NAME}"
                "$<TARGET_FILE_DIR:VoiceFlowAI>/${DLL_NAME}"
            COMMENT "Copying ${DLL_NAME} to output directory"
        )
    else()
        message(WARNING "MinGW runtime DLL not found: ${MINGW_BIN_DIR}/${DLL_NAME}")
    endif()
endforeach()
