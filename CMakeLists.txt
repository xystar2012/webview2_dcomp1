# WebView2 + DirectComposition three-layer window demo.
#
# Build with Visual Studio 2019. WebView2 SDK is consumed from the local
# NuGet cache so we do not require nuget.exe or network access during build.

cmake_minimum_required(VERSION 3.20)
project(webview2_dcomp1 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Avoid the default Debug/Release subdir layout so DLLs and assets sit next
# to the .exe - easier to launch from the IDE and from the command line.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Locate the WebView2 SDK.
# Default to the well-known NuGet cache path. Override with -DWV2_SDK_DIR=...
set(WV2_SDK_HINT "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/1.0.3179.45/build/native")
if(DEFINED WV2_SDK_DIR)
    set(WV2_SDK_HINT "${WV2_SDK_DIR}")
endif()

find_path(WV2_INCLUDE_DIR
    NAMES WebView2.h
    HINTS "${WV2_SDK_HINT}/include"
    NO_DEFAULT_PATH
)
find_path(WV2_ENVOPTS_DIR
    NAMES WebView2EnvironmentOptions.h
    HINTS "${WV2_SDK_HINT}/include"
    NO_DEFAULT_PATH
)
find_library(WV2_LOADER_LIB
    NAMES WebView2Loader.dll.lib
    HINTS "${WV2_SDK_HINT}/x64"
    NO_DEFAULT_PATH
)
find_file(WV2_LOADER_DLL
    NAMES WebView2Loader.dll
    HINTS "${WV2_SDK_HINT}/x64"
    NO_DEFAULT_PATH
)

if(NOT WV2_INCLUDE_DIR OR NOT WV2_LOADER_LIB OR NOT WV2_LOADER_DLL)
    message(FATAL_ERROR
        "WebView2 SDK not found. Set WV2_SDK_DIR to the SDK root "
        "(the folder containing build\\native).")
endif()

message(STATUS "WebView2 SDK headers : ${WV2_INCLUDE_DIR}")
message(STATUS "WebView2Loader.lib   : ${WV2_LOADER_LIB}")
message(STATUS "WebView2Loader.dll   : ${WV2_LOADER_DLL}")

# Target.
add_executable(webview2_dcomp1 WIN32
    src/app.manifest
    src/main.cpp
    src/App.cpp
    src/util.cpp
    src/MainWnd.cpp
    src/BrowserWnd.cpp
    src/VideoWnd.cpp
    src/GifAnimator.cpp
    src/PointerInfo.cpp
)

target_include_directories(webview2_dcomp1 PRIVATE
    src
    ${WV2_INCLUDE_DIR}
)

target_compile_definitions(webview2_dcomp1 PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
)

target_link_libraries(webview2_dcomp1 PRIVATE
    dcomp
    d2d1
    dwrite
    windowscodecs
    gdiplus
    ole32
    oleaut32
    user32
    gdi32
    version
    advapi32
    shell32
    shlwapi
    ${WV2_LOADER_LIB}
)

# Embed the application manifest so the OS treats the process as a real
# Windows 10/11 app. Without the supportedOS GUIDs Windows applies the
# Vista/7 compatibility shim which breaks per-monitor v2 DPI and some
# DWM/DirectComposition behaviors we depend on.
set_target_properties(webview2_dcomp1 PROPERTIES
    VS_DPI_AWARE "PerMonitor"
)

# Copy runtime dependencies next to the binary so the .exe can be launched
# directly from the build folder (no PATH manipulation needed).
add_custom_command(TARGET webview2_dcomp1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:webview2_dcomp1>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WV2_LOADER_DLL}"
        "$<TARGET_FILE_DIR:webview2_dcomp1>/WebView2Loader.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/html"
        "$<TARGET_FILE_DIR:webview2_dcomp1>/html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/marketplace.gif"
        "$<TARGET_FILE_DIR:webview2_dcomp1>/marketplace.gif"
    VERBATIM
)
