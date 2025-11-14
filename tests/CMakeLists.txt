# Testing infrastructure for voxel-engine
# Add to parent CMakeLists.txt: add_subdirectory(tests)

enable_testing()

# Get list of source files needed for tests (exclude main.cpp)
file(GLOB TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)
list(REMOVE_ITEM TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.cpp
)

# Collect ImGui sources (same as main build)
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui-1.91.9b")
file(GLOB IMGUI_TEST_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
)

# ============================================================
# Test 1: Chunk Correctness Tests
# ============================================================

add_executable(test_chunk_correctness
    chunk_correctness_test.cpp
    test_utils.cpp
    ${TEST_SOURCES}
    ${IMGUI_TEST_SOURCES}
)

target_include_directories(test_chunk_correctness PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${Vulkan_INCLUDE_DIRS}
)

if(WIN32)
    target_link_libraries(test_chunk_correctness
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw3
    )
else()
    target_link_libraries(test_chunk_correctness
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw
        ${CMAKE_DL_LIBS}
    )
endif()

# Enable Address Sanitizer on non-Windows
if(NOT MSVC)
    target_compile_options(test_chunk_correctness PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_chunk_correctness PRIVATE -fsanitize=address)
endif()

add_test(
    NAME ChunkCorrectness
    COMMAND test_chunk_correctness
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(ChunkCorrectness PROPERTIES
    TIMEOUT 30
    LABELS "fast;correctness"
)

# ============================================================
# Test 2: Memory Leak Tests
# ============================================================

add_executable(test_memory_leaks
    memory_leak_test.cpp
    test_utils.cpp
    ${TEST_SOURCES}
    ${IMGUI_TEST_SOURCES}
)

target_include_directories(test_memory_leaks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${Vulkan_INCLUDE_DIRS}
)

if(WIN32)
    target_link_libraries(test_memory_leaks
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw3
    )
else()
    target_link_libraries(test_memory_leaks
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw
        ${CMAKE_DL_LIBS}
    )
endif()

# Enable Address Sanitizer
if(NOT MSVC)
    target_compile_options(test_memory_leaks PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_memory_leaks PRIVATE -fsanitize=address)
endif()

add_test(
    NAME MemoryLeaks
    COMMAND test_memory_leaks
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(MemoryLeaks PROPERTIES
    TIMEOUT 120
    LABELS "memory"
)

# ============================================================
# Test 3: Performance Tests
# ============================================================

add_executable(test_performance
    performance_test.cpp
    test_utils.cpp
    ${TEST_SOURCES}
    ${IMGUI_TEST_SOURCES}
)

target_include_directories(test_performance PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${Vulkan_INCLUDE_DIRS}
)

if(WIN32)
    target_link_libraries(test_performance
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw3
    )
else()
    target_link_libraries(test_performance
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw
        ${CMAKE_DL_LIBS}
    )
endif()

if(NOT MSVC)
    target_compile_options(test_performance PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_performance PRIVATE -fsanitize=address)
endif()

add_test(
    NAME Performance
    COMMAND test_performance
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(Performance PROPERTIES
    TIMEOUT 60
    LABELS "performance"
)

# ============================================================
# Test 4: Stress Tests
# ============================================================

add_executable(test_stress
    stress_test.cpp
    test_utils.cpp
    ${TEST_SOURCES}
    ${IMGUI_TEST_SOURCES}
)

target_include_directories(test_stress PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${Vulkan_INCLUDE_DIRS}
)

if(WIN32)
    target_link_libraries(test_stress
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw3
    )
else()
    target_link_libraries(test_stress
        ${Vulkan_LIBRARIES}
        yaml-cpp
        glfw
        ${CMAKE_DL_LIBS}
    )
endif()

if(NOT MSVC)
    target_compile_options(test_stress PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_stress PRIVATE -fsanitize=address)
endif()

add_test(
    NAME Stress
    COMMAND test_stress
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(Stress PROPERTIES
    TIMEOUT 120
    LABELS "stress"
)

# ============================================================
# Custom test commands
# ============================================================

# Run only fast tests (correctness + basic performance)
add_custom_target(test_fast
    COMMAND ${CMAKE_CTEST_COMMAND} -L fast -V
    DEPENDS test_chunk_correctness test_performance
    COMMENT "Running fast tests..."
)

# Run all tests with verbose output
add_custom_target(test_all
    COMMAND ${CMAKE_CTEST_COMMAND} -V
    DEPENDS test_chunk_correctness test_memory_leaks test_performance test_stress
    COMMENT "Running all tests..."
)

# Run with memory checking (Valgrind on Linux)
if(UNIX AND NOT APPLE)
    add_custom_target(test_valgrind
        COMMAND valgrind --leak-check=full --show-leak-kinds=all
                --track-origins=yes --verbose
                ./test_memory_leaks
        DEPENDS test_memory_leaks
        COMMENT "Running memory leak detection with Valgrind..."
    )
endif()

message(STATUS "Tests configured. Use: ctest -V or cmake --build . --target test_all")
