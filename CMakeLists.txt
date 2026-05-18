cmake_minimum_required(VERSION 3.20)
project(Gw2ItemSearch VERSION 0.7.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui"
    CACHE PATH "Path to ImGui headers")

set(NLOHMANN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external"
    CACHE PATH "Path to nlohmann/json")

file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
    ${IMGUI_INCLUDE_DIR}/imgui.cpp
    ${IMGUI_INCLUDE_DIR}/imgui_draw.cpp
    ${IMGUI_INCLUDE_DIR}/imgui_tables.cpp
    ${IMGUI_INCLUDE_DIR}/imgui_widgets.cpp
)

add_library(Gw2ItemSearch SHARED ${SOURCES})

target_include_directories(Gw2ItemSearch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
    src
)

set_target_properties(Gw2ItemSearch PROPERTIES
    PREFIX  ""
    SUFFIX  ".dll"
    OUTPUT_NAME "Gw2ItemSearch"
)

target_compile_definitions(Gw2ItemSearch PRIVATE
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(Gw2ItemSearch PRIVATE
    winhttp
    crypt32
    -static-libgcc
    -static-libstdc++
    -Wl,-Bstatic,--whole-archive,-lwinpthread,--no-whole-archive
)
