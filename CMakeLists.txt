cmake_minimum_required(VERSION 3.20)
project(OGFNLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OGFN Launcher is 64-bit only (Fortnite builds are x64).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "OGFN Launcher only supports 64-bit builds.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "OGFN Launcher requires MSVC (Visual Studio 2022, Desktop development with C++).")
endif()

# ---------------------------------------------------------------------------
# WebView2 SDK — downloaded at configure time from NuGet (no package manager,
# nothing vendored in git). Extracted into the build directory.
# ---------------------------------------------------------------------------
set(WEBVIEW2_VERSION "1.0.4191.47")
set(WEBVIEW2_ZIP "${CMAKE_BINARY_DIR}/webview2-${WEBVIEW2_VERSION}.zip")
set(WEBVIEW2_DIR  "${CMAKE_BINARY_DIR}/webview2")
set(WEBVIEW2_HEADER "${WEBVIEW2_DIR}/build/native/include/WebView2.h")

if(NOT EXISTS "${WEBVIEW2_HEADER}")
    message(STATUS "Downloading WebView2 SDK ${WEBVIEW2_VERSION}...")
    file(DOWNLOAD
        "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}"
        "${WEBVIEW2_ZIP}"
        SHOW_PROGRESS
        STATUS WEBVIEW2_DOWNLOAD_STATUS
    )
    list(GET WEBVIEW2_DOWNLOAD_STATUS 0 WEBVIEW2_DL_CODE)
    if(NOT WEBVIEW2_DL_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download WebView2 SDK: ${WEBVIEW2_DOWNLOAD_STATUS}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${WEBVIEW2_ZIP}" DESTINATION "${WEBVIEW2_DIR}")
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_DIR}/build/native/include")
set(WEBVIEW2_LIB_DIR     "${WEBVIEW2_DIR}/build/native/x64")

# ---------------------------------------------------------------------------
# Launcher executable (Win32 GUI, no console window)
# ---------------------------------------------------------------------------
add_executable(OGFNLauncher WIN32
    src/native/main.cpp
)

target_compile_definitions(OGFNLauncher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(OGFNLauncher PRIVATE /W4 /permissive-)

target_include_directories(OGFNLauncher PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_link_directories(OGFNLauncher PRIVATE "${WEBVIEW2_LIB_DIR}")

# WebView2LoaderStatic = static loader lib (no extra DLL to ship).
# d2d1/dxgi/shcore are used by WebView2 hosting and DPI handling.
target_link_libraries(OGFNLauncher PRIVATE
    WebView2LoaderStatic
    d2d1
    dxgi
    shcore
)
