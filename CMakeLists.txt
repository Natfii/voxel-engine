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
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external
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
    find_package(glfw3 REQUIRED)
    include_directories(${Vulkan_INCLUDE_DIRS})
endif()

file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(voxel-engine ${SOURCES} ${IMGUI_SOURCES})

# Platform-specific linking
if(WIN32)
    target_link_libraries(voxel-engine
        ${Vulkan_LIBRARIES}
        glfw3
        yaml-cpp
    )
else()
    # Linux
    target_link_libraries(voxel-engine
        ${Vulkan_LIBRARIES}
        glfw
        yaml-cpp
        ${CMAKE_DL_LIBS}
    )
endif()

# Copy assets directory to build output directory
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