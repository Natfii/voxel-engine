# Testing infrastructure for voxel-engine
# Add to parent CMakeLists.txt: add_subdirectory(tests)

enable_testing()

# PERFORMANCE FIX: Link against voxel-engine-lib instead of recompiling all sources
# This reduces build time by ~80% when building tests

# ============================================================
# Test 1: Chunk Correctness Tests
# ============================================================

add_executable(test_chunk_correctness
    chunk_correctness_test.cpp
    test_utils.cpp
)

target_include_directories(test_chunk_correctness PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link against the shared library (already contains all engine code)
target_link_libraries(test_chunk_correctness PRIVATE voxel-engine-lib)

# Enable Address Sanitizer on non-Windows
if(NOT MSVC)
    target_compile_options(test_chunk_correctness PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_chunk_correctness PRIVATE -fsanitize=address)
endif()

add_test(
    NAME ChunkCorrectness
    COMMAND $<TARGET_FILE:test_chunk_correctness>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
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
)

target_include_directories(test_memory_leaks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_memory_leaks PRIVATE voxel-engine-lib)

# Enable Address Sanitizer
if(NOT MSVC)
    target_compile_options(test_memory_leaks PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_memory_leaks PRIVATE -fsanitize=address)
endif()

add_test(
    NAME MemoryLeaks
    COMMAND $<TARGET_FILE:test_memory_leaks>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
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
)

target_include_directories(test_performance PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_performance PRIVATE voxel-engine-lib)

if(NOT MSVC)
    target_compile_options(test_performance PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_performance PRIVATE -fsanitize=address)
endif()

add_test(
    NAME Performance
    COMMAND $<TARGET_FILE:test_performance>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
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
)

target_include_directories(test_stress PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_stress PRIVATE voxel-engine-lib)

if(NOT MSVC)
    target_compile_options(test_stress PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_stress PRIVATE -fsanitize=address)
endif()

add_test(
    NAME Stress
    COMMAND $<TARGET_FILE:test_stress>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(Stress PROPERTIES
    TIMEOUT 120
    LABELS "stress"
)

# ============================================================
# Test 5: Mesh Pooling Performance
# ============================================================

add_executable(test_mesh_pooling
    test_mesh_pooling.cpp
)

target_include_directories(test_mesh_pooling PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_mesh_pooling PRIVATE voxel-engine-lib)

add_test(
    NAME MeshPooling
    COMMAND $<TARGET_FILE:test_mesh_pooling>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(MeshPooling PROPERTIES
    TIMEOUT 30
    LABELS "performance;pooling"
)

# ============================================================
# Test 6: Moisture-Based Biome Selection
# ============================================================

add_executable(test_moisture_biome_selection
    test_moisture_biome_selection.cpp
)

target_include_directories(test_moisture_biome_selection PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_moisture_biome_selection PRIVATE voxel-engine-lib)

add_test(
    NAME MoistureBiomeSelection
    COMMAND $<TARGET_FILE:test_moisture_biome_selection>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(MoistureBiomeSelection PROPERTIES
    TIMEOUT 30
    LABELS "fast;correctness;biome"
)

# ============================================================
# Test 7: Biome Interpolation Utilities
# ============================================================

add_executable(test_biome_interpolation
    test_biome_interpolation.cpp
)

target_include_directories(test_biome_interpolation PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Note: This test only needs GLM headers, not full engine lib
# But we link it anyway for consistency
target_link_libraries(test_biome_interpolation PRIVATE voxel-engine-lib)

add_test(
    NAME BiomeInterpolation
    COMMAND $<TARGET_FILE:test_biome_interpolation>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(BiomeInterpolation PROPERTIES
    TIMEOUT 30
    LABELS "fast;correctness;biome;interpolation"
)

# ============================================================
# Custom test commands
# ============================================================

# Run only fast tests (correctness + basic performance)
add_custom_target(test_fast
    COMMAND ${CMAKE_CTEST_COMMAND} -L fast -V
    DEPENDS test_chunk_correctness test_performance test_moisture_biome_selection test_biome_interpolation
    COMMENT "Running fast tests..."
)

# Run all tests with verbose output
add_custom_target(test_all
    COMMAND ${CMAKE_CTEST_COMMAND} -V
    DEPENDS test_chunk_correctness test_memory_leaks test_performance test_stress test_moisture_biome_selection
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
