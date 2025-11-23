cmake_minimum_required(VERSION 3.10)

# COMPATIBILITY NOTE: Supports CMake 3.10+ and Visual Studio 2017/2019/2022
# - CMake 3.10+ (2018): Baseline for older systems
# - CMake 3.21+ (2021): Recommended for VS 2022 (version 17) and modern features
# - CMake 3.29+ (2024): Latest stable with enhanced Vulkan/C++20 support
#
# - VS 2017 (version 15): Minimum supported
# - VS 2019 (version 16): Fully supported
# - VS 2022 (version 17): Recommended (best C++20 support)
#
# This CMakeLists.txt uses features compatible with CMake 3.10 while
# taking advantage of newer features when available via conditional checks.

project(voxel-engine VERSION 1.0.0 LANGUAGES CXX)

# Use C++17 for broad compatibility (C++20 if available)
# C++17 is well-supported by VS 2017/2019/2022, GCC 7+, Clang 5+
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Modern CMake 3.21+: Use organized output directories
# Falls back gracefully for older CMake versions
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    message(STATUS "Using modern CMake ${CMAKE_VERSION} with organized output directories")
else()
    message(STATUS "Using CMake ${CMAKE_VERSION} (consider upgrading to 3.21+ for better features)")
endif()

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


# Modern CMake: Copy runtime assets to output directory
# Uses generator expressions for correct paths across configurations (Debug/Release/etc.)

add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:voxel-engine>/assets
    COMMENT "Copying assets to build directory"
)

add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.ini
        $<TARGET_FILE_DIR:voxel-engine>/config.ini
    COMMENT "Copying config.ini to build directory"
)

add_custom_command(TARGET voxel-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:voxel-engine>/shaders
    COMMENT "Copying compiled shaders to build directory"
)

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