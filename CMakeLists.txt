cmake_minimum_required(VERSION 3.20)
project(OGFNLauncher LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OGFN Launcher is 64-bit only (Fortnite builds are x64).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "OGFN Launcher only supports 64-bit builds.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "OGFN Launcher requires MSVC (Visual Studio 2022/2026, Desktop development with C++).")
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
# nlohmann/json — header-only, fetched at configure time (single header).
# ---------------------------------------------------------------------------
set(JSON_VERSION "3.11.3")
set(JSON_HEADER "${CMAKE_BINARY_DIR}/_deps/json-single/nlohmann/json.hpp")
if(NOT EXISTS "${JSON_HEADER}")
    message(STATUS "Downloading nlohmann/json ${JSON_VERSION}...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nlohmann/json/v${JSON_VERSION}/single_include/nlohmann/json.hpp"
        "${JSON_HEADER}"
        STATUS JSON_DOWNLOAD_STATUS
    )
    list(GET JSON_DOWNLOAD_STATUS 0 JSON_DL_CODE)
    if(NOT JSON_DL_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download nlohmann/json: ${JSON_DOWNLOAD_STATUS}")
    endif()
endif()
set(JSON_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/json-single")

# ---------------------------------------------------------------------------
# Launcher executable (Win32 GUI, no console window)
# ---------------------------------------------------------------------------
add_executable(OGFNLauncher WIN32
    src/native/main.cpp
    src/native/bridge.cpp
    src/native/config.cpp
    src/native/util.cpp
    src/native/log.cpp
    src/native/crypto.cpp
    src/native/accounts.cpp
    src/native/build410.cpp
    src/native/ogfnlauncher.rc
)

target_compile_definitions(OGFNLauncher PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    OGFN_VERSION="0.1.0"
)

target_compile_options(OGFNLauncher PRIVATE /W4 /permissive-)
# The manifest is embedded via ogfnlauncher.rc (DPI awareness etc.), so the
# linker must not generate a second one.
target_link_options(OGFNLauncher PRIVATE /MANIFEST:NO)

target_include_directories(OGFNLauncher PRIVATE
    "${WEBVIEW2_INCLUDE_DIR}"
    "${JSON_INCLUDE_DIR}"
    src/native
)
target_link_directories(OGFNLauncher PRIVATE "${WEBVIEW2_LIB_DIR}")

# WebView2LoaderStatic = static loader lib (no extra DLL to ship).
# d2d1/dxgi/shcore are used by WebView2 hosting and DPI handling.
# crypt32 is used for SHA-256 (bcrypt is part of the CRT set).
target_link_libraries(OGFNLauncher PRIVATE
    WebView2LoaderStatic
    d2d1
    dxgi
    shcore
    dwmapi
    bcrypt
    userenv
    oleaut32
    uuid
)

# ---------------------------------------------------------------------------
# Web assets: copied next to the exe so the launcher can find them
# (dev builds run straight from the build tree).
# ---------------------------------------------------------------------------
set(WEB_ASSETS_DIR "$<TARGET_FILE_DIR:OGFNLauncher>/web")
add_custom_command(TARGET OGFNLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/web" "${WEB_ASSETS_DIR}"
    COMMENT "Copying web assets"
)

# ---------------------------------------------------------------------------
# Installer (Inno Setup) — optional; enabled when ISCC.exe is available.
# Install Inno Setup 6 from https://jrsoftware.org/isdl.php and re-run CMake.
# ---------------------------------------------------------------------------
find_file(INNO_SETUP_ISCC
    NAMES ISCC.exe
    HINTS
        "C:/Program Files (x86)/Inno Setup 6"
        "C:/Program Files/Inno Setup 6"
    DOC "Inno Setup compiler"
)

if(INNO_SETUP_ISCC)
    message(STATUS "Inno Setup found: ${INNO_SETUP_ISCC} — 'installer' target enabled")
    add_custom_target(installer
        COMMAND "${INNO_SETUP_ISCC}"
            "/DMyAppVersion=0.1.0"
            "${CMAKE_SOURCE_DIR}/installer/ogfn-launcher.iss"
        DEPENDS OGFNLauncher
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/installer"
        COMMENT "Compiling installer with Inno Setup"
    )
else()
    message(STATUS "Inno Setup not found — 'installer' target disabled (installer script still kept in repo)")
endif()
