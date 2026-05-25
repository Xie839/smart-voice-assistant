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

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Network Concurrent)

qt_standard_project_setup()

option(VOICEFLOW_ENABLE_SHERPA_STREAMING "Enable sherpa-onnx streaming ASR backend" ON)
set(VOICEFLOW_SHERPA_STREAMING_COMPILED OFF)
set(VOICEFLOW_SHERPA_STREAMING_HEADERS OFF)
set(VOICEFLOW_HAS_SHERPA_ONNX_STREAMING 0)

if(VOICEFLOW_ENABLE_SHERPA_STREAMING)
    file(GLOB VOICEFLOW_SHERPA_C_API_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx*/include/sherpa-onnx/c-api/c-api.h"
    )
    file(GLOB VOICEFLOW_SHERPA_MINGW_LIBS
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx*/lib/libsherpa-onnx-c-api.a"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx*/lib/libsherpa-onnx-cxx-api.a"
    )
    file(GLOB VOICEFLOW_SHERPA_MSVC_LIBS
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx*/lib/sherpa-onnx-c-api.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx*/lib/sherpa-onnx-cxx-api.lib"
    )

    if(VOICEFLOW_SHERPA_C_API_HEADERS)
        list(GET VOICEFLOW_SHERPA_C_API_HEADERS 0 VOICEFLOW_SHERPA_C_API_HEADER)
        get_filename_component(VOICEFLOW_SHERPA_C_API_DIR "${VOICEFLOW_SHERPA_C_API_HEADER}" DIRECTORY)
        get_filename_component(VOICEFLOW_SHERPA_INCLUDE_DIR "${VOICEFLOW_SHERPA_C_API_DIR}/../.." ABSOLUTE)
        set(VOICEFLOW_SHERPA_STREAMING_HEADERS ON)
        set(VOICEFLOW_HAS_SHERPA_ONNX_STREAMING 1)
    endif()

    if(VOICEFLOW_SHERPA_C_API_HEADERS AND VOICEFLOW_SHERPA_MINGW_LIBS)
        message(STATUS "sherpa-onnx streaming SDK headers and MinGW-compatible import library found. Streaming backend will use runtime DLL loading.")
    elseif(MINGW AND VOICEFLOW_SHERPA_C_API_HEADERS AND VOICEFLOW_SHERPA_MSVC_LIBS)
        message(STATUS "sherpa-onnx C/C++ headers and .lib import libraries found. Streaming backend will avoid linking and use runtime DLL loading.")
    else()
        message(WARNING "sherpa-onnx streaming C/C++ SDK was not found. Streaming ASR will compile as fallback-only.")
    endif()
endif()

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
    src/media/FfmpegAudioConverter.h
    src/media/FfmpegAudioConverter.cpp
    src/utils/PerfTracer.h
    src/utils/PerfTracer.cpp
    src/asr/AsrResult.h
    src/asr/TranscriptAssembler.h
    src/asr/TranscriptAssembler.cpp
    src/asr/TextPostProcessor.h
    src/asr/TextPostProcessor.cpp
    src/asr/PunctuationProcessor.h
    src/asr/PunctuationProcessor.cpp
    src/asr/SherpaOnnxAsrEngine.h
    src/asr/SherpaOnnxAsrEngine.cpp
    src/asr/SherpaOnnxStreamingAsrEngine.h
    src/asr/SherpaOnnxStreamingAsrEngine.cpp
    src/asr/AsrBackendManager.h
    src/asr/AsrBackendManager.cpp
    src/history/HistoryRecord.h
    src/history/HistoryManager.h
    src/history/HistoryManager.cpp
    src/config/AppConfig.h
    src/config/AppConfig.cpp
    src/ai/DeepSeekClient.h
    src/ai/DeepSeekClient.cpp
    src/ai/AiConfigDialog.h
    src/ai/AiConfigDialog.cpp
    src/ai/CustomPromptDialog.h
    src/ai/CustomPromptDialog.cpp
)

target_link_libraries(VoiceFlowAI PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network Qt6::Concurrent)

if(VOICEFLOW_SHERPA_STREAMING_COMPILED)
    target_compile_definitions(VoiceFlowAI PRIVATE VOICEFLOW_SHERPA_STREAMING_COMPILED=1)
endif()

if(VOICEFLOW_SHERPA_STREAMING_HEADERS)
    target_include_directories(VoiceFlowAI PRIVATE "${VOICEFLOW_SHERPA_INCLUDE_DIR}")
    target_compile_definitions(VoiceFlowAI PRIVATE VOICEFLOW_SHERPA_STREAMING_HEADERS=1)
endif()

target_compile_definitions(VoiceFlowAI PRIVATE VOICEFLOW_HAS_SHERPA_ONNX_STREAMING=${VOICEFLOW_HAS_SHERPA_ONNX_STREAMING})

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
