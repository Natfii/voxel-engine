cmake_minimum_required(VERSION 3.10)
project(voxel-engine)

set(CMAKE_CXX_STANDARD 17)

# Add external dependencies
add_subdirectory(external/yaml-cpp)

# ImGui paths
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui-1.91.9b")
file(GLOB IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/glew-2.1.0/include
    ${CMAKE_SOURCE_DIR}/external/glfw-3.4/include
    ${CMAKE_SOURCE_DIR}/external/glm
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

link_directories(
    ${CMAKE_SOURCE_DIR}/external/glew-2.1.0/lib/Release/x64
    ${CMAKE_SOURCE_DIR}/external/glfw-3.4/lib-vc2022
)

file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(voxel-engine ${SOURCES} ${IMGUI_SOURCES})

target_link_libraries(voxel-engine
    opengl32
    glew32s
    glfw3
    yaml-cpp
)

add_definitions(-DGLEW_STATIC)