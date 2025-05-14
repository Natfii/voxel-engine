if (MSVC)
  # CMake 3.15+ property for /MD
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
cmake_minimum_required(VERSION 3.10)
project(voxel_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# — Vendored GLFW (adjust version) —
set(GLFW_DIR    "${CMAKE_SOURCE_DIR}/external/glfw-3.4")
set(GLFW_INCLUDE_DIR "${GLFW_DIR}/include")
set(GLFW_LIBRARY     "${GLFW_DIR}/lib-vc2019/glfw3.lib")

# — GLEW (static) —
set(GLEW_DIR    "${CMAKE_SOURCE_DIR}/external/glew-2.1.0")
set(GLEW_INCLUDE_DIR "${GLEW_DIR}/include")
set(GLEW_LIBRARY     "${GLEW_DIR}/lib/Release/x64/glew32s.lib") 
add_definitions(-DGLEW_STATIC)                                  

# - yaml-cpp - 
set(YAML_DIR "${CMAKE_SOURCE_DIR}/external/yaml-cpp")
set(YAML_INCLUDE_DIR "${YAML_DIR}/include")
add_subdirectory("${YAML_DIR}")

# — OpenGL system library —
find_package(OpenGL REQUIRED)

# — Include paths — 
include_directories(
    "${CMAKE_SOURCE_DIR}/include"
    "${GLEW_INCLUDE_DIR}"
    "${GLFW_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}/external/glm"
    "${YAML_INCLUDE_DIR}/yaml-cpp"
)

# — Build target —
file(GLOB VOXEL_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_executable(voxel-engine
    src/main.cpp
    src/chunk.cpp
    src/block_system.cpp
    src/world.cpp
)

# - VS Debugger - 
set_target_properties(voxel-engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# — Link dependencies —
target_link_libraries(voxel-engine
    ${OPENGL_gl_LIBRARY}
    ${GLEW_LIBRARY}
    ${GLFW_LIBRARY}
    legacy_stdio_definitions
    ucrt
    user32
    gdi32
    shell32
    yaml-cpp
)

# - Assets -
add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:voxel-engine>/assets"
)