cmake_minimum_required(VERSION 3.10)
project(voxel-engine)

set(CMAKE_CXX_STANDARD 17)

# Define GLFW to not include OpenGL headers
add_compile_definitions(GLFW_INCLUDE_VULKAN GLFW_INCLUDE_NONE)

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
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/tinygltf
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Platform-specific configuration
if(WIN32)
    include_directories(
        ${CMAKE_SOURCE_DIR}/external/glfw-3.4/include
    )
    link_directories(
        ${CMAKE_SOURCE_DIR}/external/glfw-3.4/lib-vc2022
    )
    # Find Vulkan SDK on Windows
    find_package(Vulkan REQUIRED)
    include_directories(${Vulkan_INCLUDE_DIRS})
else()
    # Linux: use system libraries
    find_package(Vulkan REQUIRED)
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        message(STATUS "glfw3 package not found, will link directly with -lglfw")
    endif()
    include_directories(${Vulkan_INCLUDE_DIRS})
endif()

# PERFORMANCE FIX: Create a library for common code to avoid recompiling for tests
# This reduces build time from 5x to 1x when tests are enabled
file(GLOB_RECURSE ENGINE_SOURCES src/*.cpp)
list(REMOVE_ITEM ENGINE_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

# Create shared library with all engine code (except main.cpp)
add_library(voxel-engine-lib STATIC ${ENGINE_SOURCES} ${IMGUI_SOURCES})

target_include_directories(voxel-engine-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/tinygltf
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${Vulkan_INCLUDE_DIRS}
)

# Platform-specific includes for library
if(WIN32)
    target_include_directories(voxel-engine-lib PUBLIC
        ${CMAKE_SOURCE_DIR}/external/glfw-3.4/include
    )
endif()

# Link libraries to the shared library
if(WIN32)
    target_link_libraries(voxel-engine-lib PUBLIC
        ${Vulkan_LIBRARIES}
        glfw3
        yaml-cpp
    )
else()
    if(glfw3_FOUND)
        target_link_libraries(voxel-engine-lib PUBLIC
            ${Vulkan_LIBRARIES}
            glfw
            yaml-cpp
            ${CMAKE_DL_LIBS}
        )
    else()
        target_link_libraries(voxel-engine-lib PUBLIC
            ${Vulkan_LIBRARIES}
            -lglfw
            yaml-cpp
            ${CMAKE_DL_LIBS}
        )
    endif()
endif()

# Main executable just links against the library
add_executable(voxel-engine src/main.cpp)
target_link_libraries(voxel-engine PRIVATE voxel-engine-lib)


# Copy assets directory to build output directory
# (includes blocks, biomes, textures, and all subdirectories)
add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:voxel-engine>/assets
    COMMENT "Copying assets to build directory"
)

# Copy config.ini to build output directory
add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.ini
        $<TARGET_FILE_DIR:voxel-engine>/config.ini
    COMMENT "Copying config.ini to build directory"
)

# Copy shaders directory to build output directory
add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:voxel-engine>/shaders
    COMMENT "Copying shaders to build directory"
)

# Copy docs directory to build output directory
add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/docs
        $<TARGET_FILE_DIR:voxel-engine>/docs
    COMMENT "Copying documentation to build directory"
)

# Enable testing support (MUST be in root CMakeLists.txt)
enable_testing()

# Add tests subdirectory
add_subdirectory(tests)