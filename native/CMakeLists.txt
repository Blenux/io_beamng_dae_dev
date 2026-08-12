cmake_minimum_required(VERSION 3.15)
project(cdae_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# macOS: set deployment target and architecture for wheel platform tags
if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
endif()

# --- pugixml ---
# Try find_package first; fall back to FetchContent.
find_package(pugixml CONFIG QUIET)
if(NOT pugixml_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        pugixml
        GIT_REPOSITORY https://github.com/zeux/pugixml.git
        GIT_TAG v1.14
    )
    FetchContent_MakeAvailable(pugixml)
endif()

# --- zstd ---
# Try find_package first; fall back to FetchContent (lib only, no CLI).
find_package(zstd CONFIG QUIET)
if(NOT zstd_FOUND)
    include(FetchContent)
    # Disable everything except static lib for cross-platform compatibility
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        SOURCE_SUBDIR build/cmake
    )
    FetchContent_MakeAvailable(zstd)
endif()

# Detect available zstd target name (varies between find_package and FetchContent)
if(TARGET zstd::libzstd_static)
    set(ZSTD_LIB zstd::libzstd_static)
elseif(TARGET zstd::libzstd_shared)
    set(ZSTD_LIB zstd::libzstd_shared)
elseif(TARGET libzstd_static)
    set(ZSTD_LIB libzstd_static)
elseif(TARGET libzstd_shared)
    set(ZSTD_LIB libzstd_shared)
elseif(TARGET zstd::libzstd)
    set(ZSTD_LIB zstd::libzstd)
else()
    message(FATAL_ERROR "Could not find a suitable zstd target")
endif()

# --- pybind11 ---
find_package(pybind11 CONFIG REQUIRED)

# --- cdae_native module ---
pybind11_add_module(cdae_native
    src/cdae_native.cpp
    src/dae_import.cpp
    src/dae_export.cpp
    src/cdae_binary_import.cpp
    src/cdae_binary_export.cpp
    src/msgpack_reader.cpp
)

target_link_libraries(cdae_native PRIVATE pugixml::pugixml ${ZSTD_LIB})
target_include_directories(cdae_native PRIVATE src)

# Strip symbols for smaller binary
set_target_properties(cdae_native PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

# Install to root of package
install(TARGETS cdae_native DESTINATION .)
