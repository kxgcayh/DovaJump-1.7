cmake_minimum_required(VERSION 3.24)

project(DovaJump VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIBSSE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Current local CommonLibSSE-NG
set(CommonLibPath "C:/Users/spenc/Documents/Code/GitHub/CommonLibSSE-NG")

add_subdirectory(
    ${CommonLibPath}
    CommonLibSSE
    EXCLUDE_FROM_ALL
)

include("${CommonLibPath}/cmake/CommonLibSSE.cmake")

add_commonlibsse_plugin(DovaJump
    SOURCES
        src/main.cpp
        src/Skyphoria.cpp
        util/Requirements.cpp
)
target_precompile_headers(DovaJump
    PRIVATE
        src/PCH.h
)

target_include_directories(DovaJump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)