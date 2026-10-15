cmake_minimum_required(VERSION 3.24)

project(DovaJump VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIBSSE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

# Locate CommonLibSSE-NG submodule
if(NOT DEFINED CommonLibPath)
    if(DEFINED ENV{CommonLibPath})
        set(CommonLibPath "$ENV{CommonLibPath}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG/CMakeLists.txt")
        set(CommonLibPath "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG")
    endif()
endif()

if(DEFINED CommonLibPath AND EXISTS "${CommonLibPath}/CMakeLists.txt")
    message(STATUS "Using CommonLibSSE-NG submodule from: ${CommonLibPath}")
    add_subdirectory(
        ${CommonLibPath}
        CommonLibSSE
        EXCLUDE_FROM_ALL
    )
else()
    message(FATAL_ERROR "CommonLibSSE-NG submodule not found. Please run: git submodule update --init --recursive")
endif()

add_compile_definitions(NOMINMAX)

add_library(DovaJump SHARED
    src/main.cpp
    src/Skyphoria.cpp
    util/Requirements.cpp
)

target_link_libraries(DovaJump PRIVATE CommonLibSSE::CommonLibSSE)

target_precompile_headers(DovaJump
    PRIVATE
        src/PCH.h
)

# Search for SimpleIni / vcpkg includes
find_path(SIMPLEINI_INCLUDE_DIR "SimpleIni.h"
    PATHS
        "$ENV{VCPKG_ROOT}/installed/x64-windows/include"
        "$ENV{VCPKG_ROOT}/installed/x64-windows-static/include"
        "$ENV{VCPKG_ROOT}/installed/x64-windows-cross/include"
        "/Volumes/S512/Projects/ETC/vcpkg/installed/x64-windows-cross/include"
)

target_include_directories(DovaJump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    $<$<BOOL:${SIMPLEINI_INCLUDE_DIR}>:${SIMPLEINI_INCLUDE_DIR}>
)