#pragma once

/**
 * @file test_utils.h
 * @brief Utilities for testing voxel engine components
 *
 * Provides mock objects and testing helpers without external test frameworks
 */

#include <iostream>
#include <stdexcept>
#include <chrono>
#include <vector>

// ============================================================
// Test Assertion Macros
// ============================================================

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                   " ASSERT_TRUE failed: " #condition); \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                   " ASSERT_EQ failed: " #a " != " #b); \
        } \
    } while(0)

#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_LE(a, b) ASSERT_TRUE((a) <= (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_GE(a, b) ASSERT_TRUE((a) >= (b))

#define ASSERT_NULL(ptr) ASSERT_EQ((ptr), nullptr)
#define ASSERT_NOT_NULL(ptr) ASSERT_TRUE((ptr) != nullptr)

// ============================================================
// Test Results Tracking
// ============================================================

struct TestResult {
    std::string name;
    bool passed;
    std::string error;
    double duration_ms;
};

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner runner;
        return runner;
    }

    void add_result(const TestResult& result) {
        results.push_back(result);
    }

    void print_summary() {
        std::cout << "\n========================================\n";
        std::cout << "TEST SUMMARY\n";
        std::cout << "========================================\n";

        int passed = 0, failed = 0;
        double total_time = 0.0;

        for (const auto& result : results) {
            if (result.passed) {
                std::cout << "✓ " << result.name << " (" << result.duration_ms << " ms)\n";
                passed++;
            } else {
                std::cout << "✗ " << result.name << " (" << result.duration_ms << " ms)\n";
                std::cout << "  ERROR: " << result.error << "\n";
                failed++;
            }
            total_time += result.duration_ms;
        }

        std::cout << "\n" << passed << " passed, " << failed << " failed\n";
        std::cout << "Total time: " << total_time << " ms\n";
        std::cout << "========================================\n";

        if (failed > 0) {
            throw std::runtime_error(std::to_string(failed) + " test(s) failed");
        }
    }

private:
    std::vector<TestResult> results;
};

// ============================================================
// Test Macros for Running Tests
// ============================================================

#define TEST(test_name) \
    void test_##test_name(); \
    namespace { \
        struct TestRunner_##test_name { \
            TestRunner_##test_name() { \
                register_test(#test_name, &test_##test_name); \
            } \
        } runner_##test_name; \
    } \
    void test_##test_name()

static std::vector<std::pair<std::string, void(*)()>> all_tests;

inline void register_test(const std::string& name, void (*fn)()) {
    all_tests.push_back({name, fn});
}

inline void run_all_tests() {
    for (const auto& [name, fn] : all_tests) {
        TestResult result;
        result.name = name;

        auto start = std::chrono::high_resolution_clock::now();
        try {
            fn();
            result.passed = true;
        } catch (const std::exception& e) {
            result.passed = false;
            result.error = e.what();
        }
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        TestRunner::instance().add_result(result);
    }

    TestRunner::instance().print_summary();
}

// ============================================================
// Mock Vulkan Renderer for Testing
// ============================================================

#include <vulkan/vulkan.h>

class MockVulkanRenderer {
public:
    // Mock Vulkan handles (all null, just for compilation)
    VkDevice getDevice() const { return nullptr; }
    VkPhysicalDevice getPhysicalDevice() const { return nullptr; }
    VkCommandBuffer getCurrentCommandBuffer() const { return nullptr; }
    VkQueue getGraphicsQueue() const { return nullptr; }
    uint32_t getGraphicsQueueFamily() const { return 0; }

    // Mock buffer creation
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
        // Allocate dummy handles
        static VkBuffer dummyBuffer = reinterpret_cast<VkBuffer>(0x1000);
        static VkDeviceMemory dummyMemory = reinterpret_cast<VkDeviceMemory>(0x2000);
        buffer = dummyBuffer++;
        bufferMemory = dummyMemory++;
        return true;
    }

    // Mock buffer destruction
    void destroyBuffer(VkBuffer buffer, VkDeviceMemory bufferMemory) {
        // No-op
    }

    // Mock copy operations
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        // No-op
    }

    bool mapMemory(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                   void** ppData) {
        // Allocate actual memory for testing
        *ppData = malloc(size);
        return true;
    }

    void unmapMemory(VkDeviceMemory memory) {
        // No-op (in real test would free)
    }
};

extern MockVulkanRenderer g_testRenderer;

// ============================================================
// Mock Biome System
// ============================================================

class MockBiomeMap {
public:
    int getTerrainHeightAt(float x, float z) const {
        return 30;  // Constant height for testing
    }

    int getBiomeAt(float x, float z) const {
        return 0;  // Default biome
    }

    float getTemperatureAt(float x, float z) const {
        return 0.5f;
    }

    float getHumidityAt(float x, float z) const {
        return 0.5f;
    }
};

extern MockBiomeMap g_testBiomeMap;

// ============================================================
// Performance Timing
// ============================================================

class ScopedTimer {
public:
    ScopedTimer(const std::string& name) : name_(name) {
        start_ = std::chrono::high_resolution_clock::now();
    }

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        std::cout << "  " << name_ << ": " << ms << " ms\n";
    }

    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};
