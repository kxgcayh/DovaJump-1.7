cmake_minimum_required(VERSION 3.24)
project(DovaJump VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIBSSE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(external/CommonLibSSE-NG)

add_library(DovaJump SHARED
    src/main.cpp
    src/Skyphoria.cpp
)
target_link_libraries(DovaJump PRIVATE CommonLibSSE::CommonLibSSE)