cmake_minimum_required(VERSION 3.22)

# IMPORTANT: must be set before project()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(praxis VERSION 0.4.0 LANGUAGES CXX)

# =========================
# Version information
# =========================
# The PyQInt and PyDFT versions that are installed into the managed Python
# environment. Every release of the GUI is tested against exactly these versions.
set(PYQINT_PINNED_VERSION "1.4.3" CACHE STRING "PyQInt version installed into the managed environment")
set(PYDFT_PINNED_VERSION "1.0.0" CACHE STRING "PyDFT version installed into the managed environment")
set(PYTHON_MANAGED_VERSION "3.12" CACHE STRING "Python version installed into the managed environment")

execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in ${CMAKE_BINARY_DIR}/config.h @ONLY)

# =========================
# C++ setup
# =========================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()

# =========================
# Dependencies
# =========================
find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Concurrent
)
find_package(glm REQUIRED)

# =========================
# Core library (all project code except main)
# =========================
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(praxis_core STATIC ${CORE_SOURCES})

target_include_directories(praxis_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(praxis_core
    PUBLIC
        glm::glm
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Concurrent
)

# =========================
# Main executable
# =========================
set(APP_SOURCES
    src/main.cpp
    resources.qrc
)

if(WIN32)
    list(APPEND APP_SOURCES resources.rc)
endif()

if(APPLE)
    set(APP_ICON_MACOS ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/praxis.icns)
    if(EXISTS ${APP_ICON_MACOS})
        set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        list(APPEND APP_SOURCES ${APP_ICON_MACOS})
    endif()
    add_executable(praxis MACOSX_BUNDLE ${APP_SOURCES})
    set_target_properties(praxis PROPERTIES
        OUTPUT_NAME "Praxis"
        MACOSX_BUNDLE_BUNDLE_NAME "Praxis"
        MACOSX_BUNDLE_GUI_IDENTIFIER "nl.tue.praxis"
        MACOSX_BUNDLE_ICON_FILE "praxis.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
else()
    add_executable(praxis ${APP_SOURCES})
endif()

if(WIN32)
    set_target_properties(praxis PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(praxis PRIVATE praxis_core)

# =========================
# Bundled uv
# =========================
# uv installs the private Python environment on first launch. The program
# looks for it next to the executable, so fetch it into the build tree as
# well; this makes development builds work without any extra steps.
option(PRAXIS_FETCH_UV "Download the pinned uv executable next to the program" ON)
if(PRAXIS_FETCH_UV)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" UV_ARCH)
    if(UV_ARCH MATCHES "^(x86_64|amd64)$")
        set(UV_ARCH "x86_64")
    elseif(UV_ARCH MATCHES "^(arm64|aarch64)$")
        set(UV_ARCH "aarch64")
    endif()

    if(WIN32)
        set(UV_TARGET "${UV_ARCH}-pc-windows-msvc")
        set(UV_BINARY "uv.exe")
    elseif(APPLE)
        set(UV_TARGET "${UV_ARCH}-apple-darwin")
        set(UV_BINARY "uv")
    else()
        set(UV_TARGET "${UV_ARCH}-unknown-linux-gnu")
        set(UV_BINARY "uv")
    endif()

    if(APPLE)
        set(UV_DEST "${CMAKE_BINARY_DIR}/Praxis.app/Contents/MacOS")
    else()
        set(UV_DEST "${CMAKE_BINARY_DIR}")
    endif()

    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
        add_custom_command(
            OUTPUT "${UV_DEST}/${UV_BINARY}"
            COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch-uv.sh" "${UV_TARGET}" "${UV_DEST}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch-uv.sh"
            COMMENT "Fetching uv (${UV_TARGET})"
            VERBATIM
        )
        add_custom_target(fetch_uv ALL DEPENDS "${UV_DEST}/${UV_BINARY}")
        add_dependencies(fetch_uv praxis)
    else()
        message(WARNING "bash not found: uv is not fetched; place uv next to the executable or on the PATH")
    endif()
endif()

# =========================
# Tests
# =========================
option(PRAXIS_BUILD_TESTS "Build unit tests" ON)
if(PRAXIS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(test)
endif()
