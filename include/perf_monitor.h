/**
 * @file perf_monitor.h
 * @brief Performance monitoring and profiling system
 *
 * Tracks timing and queue sizes for performance-critical systems.
 * Helps identify bottlenecks as player moves away from spawn.
 *
 * Created: 2025-11-24
 */

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <glm/glm.hpp>

/**
 * @brief Scoped timer for automatic timing measurements
 *
 * Usage:
 * @code
 *   {
 *       ScopedTimer timer("decoration_processing");
 *       // ... code to measure ...
 *   }  // Timer automatically records time when destroyed
 * @endcode
 */
class ScopedTimer {
public:
    ScopedTimer(const std::string& label);
    ~ScopedTimer();

    // No copying
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string m_label;
    std::chrono::high_resolution_clock::time_point m_start;
};

/**
 * @brief Performance frame data snapshot
 */
struct PerfFrameData {
    float frameTime;                    // Total frame time (ms)
    float inputTime;                    // Input processing time (ms)
    float streamingTime;                // World streaming time (ms)
    float decorationTime;               // Decoration processing time (ms)
    float chunkProcessTime;             // Chunk upload processing time (ms)
    float renderTime;                   // Rendering time (ms)

    size_t pendingDecorations;          // Chunks waiting for decoration
    size_t decorationsInProgress;       // Chunks currently decorating
    size_t pendingLoads;                // Chunks in load queue
    size_t completedChunks;             // Chunks ready for upload
    size_t meshQueueSize;               // Chunks waiting for mesh generation

    float distanceFromSpawn;            // Player distance from spawn (blocks)
    glm::vec3 playerPosition;           // Current player position
};

/**
 * @brief Singleton performance monitor
 *
 * Collects timing data and queue sizes across the engine.
 * Provides periodic summary reports to identify bottlenecks.
 */
class PerformanceMonitor {
public:
    static PerformanceMonitor& instance();

    // Recording API
    void recordTiming(const std::string& label, float milliseconds);
    void recordQueueSize(const std::string& label, size_t size);
    void recordPlayerPosition(const glm::vec3& position, const glm::vec3& spawnPosition);

    // Frame boundary
    void beginFrame();
    void endFrame();

    // Reporting
    void printReport();              // Print summary report
    void setReportInterval(float seconds);  // How often to print reports
    bool shouldPrintReport() const;  // Check if report is due

    // Enable/disable
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // Get current frame data
    const PerfFrameData& getCurrentFrame() const { return m_currentFrame; }

    // Get statistics
    float getAverageFPS() const;
    float getAverageFrameTime() const;
    float getWorstFrameTime() const;

private:
    PerformanceMonitor();
    ~PerformanceMonitor() = default;

    // No copying
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    bool m_enabled;
    float m_reportInterval;          // Seconds between reports
    float m_timeSinceLastReport;     // Accumulator

    PerfFrameData m_currentFrame;    // Current frame being recorded
    std::vector<PerfFrameData> m_frameHistory;  // Recent frames (for averaging)

    std::unordered_map<std::string, float> m_timings;     // Current frame timings
    std::unordered_map<std::string, size_t> m_queueSizes; // Current frame queue sizes

    glm::vec3 m_spawnPosition;       // Spawn position for distance calculation

    std::chrono::high_resolution_clock::time_point m_frameStart;
    mutable std::mutex m_mutex;

    // Helper methods
    void resetFrameData();
};

/**
 * @brief Helper macro for automatic timing
 *
 * Usage:
 * @code
 *   PERF_SCOPE("decoration_processing");
 *   // ... code to measure ...
 * @endcode
 */
#define PERF_SCOPE(label) ScopedTimer __perf_timer_##__LINE__(label)
