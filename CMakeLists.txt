cmake_minimum_required(VERSION 3.25.0)
project(MetasequoiaVoiceInput VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Windows-specific settings
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_definitions(
        /D_UNICODE=1
        /DUNICODE=1
        /DNOMINMAX
    )
endif()

# Dependencies
find_package(fmt CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)

if(NOT DEFINED VCPKG_INSTALLED_DIR)
    set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif()
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static")
endif()

# miniaudio
include_directories(third_party/miniaudio)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/whisper.cpp whisper-build EXCLUDE_FROM_ALL)

# Source files (Explicit list to avoid GLOB issues)
set(SOURCE_FILES
    "src/audio_capture.cpp"
    "src/audio_capture.h"
    "src/cloud_stt_worker.cpp"
    "src/cloud_stt_worker.h"
    "src/whisper_worker.cpp"
    "src/whisper_worker.h"
    "src/cue_player.cpp"
    "src/cue_player.h"
    "src/main.cpp"
    "src/resource/resource.h"
    "src/resource/app.rc"
    "src/mvi_config.cpp"
    "src/mvi_config.h"
    "src/mvi_logger.cpp"
    "src/mvi_logger.h"
    "src/mvi_utils.cpp"
    "src/mvi_utils.h"
    "src/json_websocket_stt_session.cpp"
    "src/json_websocket_stt_session.h"
    "src/json_websocket_streaming_worker.cpp"
    "src/json_websocket_streaming_worker.h"
    "src/send_input.cpp"
    "src/send_input.h"
    "src/stt_service.h"
    "src/streaming_stt_session.h"
    "src/text_polisher.cpp"
    "src/text_polisher.h"
    "src/vad.cpp"
    "src/vad.h"
    "src/wave_overlay.cpp"
    "src/wave_overlay.h"
    "src/wav_writer.h"
    "src/window_webview2.cpp"
    "src/window_webview2.h"
)

set(MY_EXECUTABLE_NAME "MetasequoiaVoiceInput")
add_executable(${MY_EXECUTABLE_NAME} ${SOURCE_FILES})

# Build as a GUI app so no console window is shown at runtime.
if(MSVC)
    target_link_options(${MY_EXECUTABLE_NAME}
        PRIVATE
        /SUBSYSTEM:WINDOWS
        /ENTRY:mainCRTStartup
    )
endif()

# Link libraries
target_link_libraries(${MY_EXECUTABLE_NAME}
    PRIVATE
    fmt::fmt
    whisper
    CURL::libcurl
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    unofficial::webview2::webview2
    ixwebsocket::ixwebsocket
    d2d1
    User32
    dwmapi
)

# Target include directories
target_include_directories(${MY_EXECUTABLE_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
)

install(TARGETS ${MY_EXECUTABLE_NAME} RUNTIME DESTINATION ".")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/config.toml" DESTINATION "templates" RENAME "config.toml")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/audios" DESTINATION ".")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/html" DESTINATION ".")

get_target_property(WEBVIEW2_DLL_DEBUG unofficial::webview2::webview2 IMPORTED_LOCATION_DEBUG)
get_target_property(WEBVIEW2_DLL_RELEASE unofficial::webview2::webview2 IMPORTED_LOCATION_RELEASE)
set(WEBVIEW2_LOADER_CANDIDATES ${WEBVIEW2_DLL_RELEASE} ${WEBVIEW2_DLL_DEBUG})
list(FILTER WEBVIEW2_LOADER_CANDIDATES EXCLUDE REGEX "-NOTFOUND$")
if(WEBVIEW2_LOADER_CANDIDATES)
    install(FILES ${WEBVIEW2_LOADER_CANDIDATES} DESTINATION ".")
endif()
