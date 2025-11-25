/**
 * @file perf_monitor.cpp
 * @brief Implementation of performance monitoring system
 *
 * Created: 2025-11-24
 */

#include "perf_monitor.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>

// ============================================================================
// ScopedTimer Implementation
// ============================================================================

ScopedTimer::ScopedTimer(const std::string& label)
    : m_label(label)
    , m_start(std::chrono::high_resolution_clock::now())
{
}

ScopedTimer::~ScopedTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start);
    float milliseconds = duration.count() / 1000.0f;

    PerformanceMonitor::instance().recordTiming(m_label, milliseconds);
}

// ============================================================================
// PerformanceMonitor Implementation
// ============================================================================

PerformanceMonitor::PerformanceMonitor()
    : m_enabled(false)
    , m_reportInterval(5.0f)  // Report every 5 seconds by default
    , m_timeSinceLastReport(0.0f)
    , m_spawnPosition(0.0f, 0.0f, 0.0f)
{
    m_frameHistory.reserve(600);  // 10 seconds at 60 FPS
    resetFrameData();
}

PerformanceMonitor& PerformanceMonitor::instance() {
    static PerformanceMonitor instance;
    return instance;
}

void PerformanceMonitor::recordTiming(const std::string& label, float milliseconds) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_timings[label] = milliseconds;
}

void PerformanceMonitor::recordQueueSize(const std::string& label, size_t size) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_queueSizes[label] = size;
}

void PerformanceMonitor::recordPlayerPosition(const glm::vec3& position, const glm::vec3& spawnPosition) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentFrame.playerPosition = position;
    m_spawnPosition = spawnPosition;

    // Calculate distance from spawn
    glm::vec3 delta = position - spawnPosition;
    m_currentFrame.distanceFromSpawn = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

void PerformanceMonitor::beginFrame() {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameStart = std::chrono::high_resolution_clock::now();
    m_timings.clear();
    m_queueSizes.clear();
}

void PerformanceMonitor::endFrame() {
    if (!m_enabled) return;

    // CRITICAL FIX (2025-11-24): Avoid deadlock by not holding lock when calling printReport()
    bool shouldPrint = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - m_frameStart);
        m_currentFrame.frameTime = duration.count() / 1000.0f;

        // Collect timing data
        m_currentFrame.inputTime = m_timings.count("input") ? m_timings["input"] : 0.0f;
        m_currentFrame.streamingTime = m_timings.count("streaming") ? m_timings["streaming"] : 0.0f;
        m_currentFrame.decorationTime = m_timings.count("decoration") ? m_timings["decoration"] : 0.0f;
        m_currentFrame.chunkProcessTime = m_timings.count("chunk_process") ? m_timings["chunk_process"] : 0.0f;
        m_currentFrame.renderTime = m_timings.count("render") ? m_timings["render"] : 0.0f;

        // Collect queue sizes
        m_currentFrame.pendingDecorations = m_queueSizes.count("pending_decorations") ? m_queueSizes["pending_decorations"] : 0;
        m_currentFrame.decorationsInProgress = m_queueSizes.count("decorations_in_progress") ? m_queueSizes["decorations_in_progress"] : 0;
        m_currentFrame.pendingLoads = m_queueSizes.count("pending_loads") ? m_queueSizes["pending_loads"] : 0;
        m_currentFrame.completedChunks = m_queueSizes.count("completed_chunks") ? m_queueSizes["completed_chunks"] : 0;
        m_currentFrame.meshQueueSize = m_queueSizes.count("mesh_queue") ? m_queueSizes["mesh_queue"] : 0;

        // Archive frame data (without printing)
        m_frameHistory.push_back(m_currentFrame);

        // Keep only recent history (last 600 frames = 10 seconds at 60 FPS)
        if (m_frameHistory.size() > 600) {
            m_frameHistory.erase(m_frameHistory.begin());
        }

        // Update report timer
        m_timeSinceLastReport += m_currentFrame.frameTime / 1000.0f;  // Convert ms to seconds
        if (m_timeSinceLastReport >= m_reportInterval) {
            shouldPrint = true;
            m_timeSinceLastReport = 0.0f;
        }
    }  // Release lock here

    // Print report OUTSIDE the lock to avoid deadlock
    if (shouldPrint) {
        printReport();
    }
}

void PerformanceMonitor::printReport() {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_frameHistory.empty()) {
        std::cout << "\n=== Performance Report ===\n";
        std::cout << "No data collected yet.\n";
        std::cout << "========================\n\n";
        return;
    }

    // Calculate averages
    float avgFrameTime = 0.0f;
    float avgInputTime = 0.0f;
    float avgStreamingTime = 0.0f;
    float avgDecorationTime = 0.0f;
    float avgChunkProcessTime = 0.0f;
    float avgRenderTime = 0.0f;

    size_t avgPendingDecorations = 0;
    size_t avgDecorationsInProgress = 0;
    size_t avgPendingLoads = 0;
    size_t avgCompletedChunks = 0;
    size_t avgMeshQueue = 0;

    float maxFrameTime = 0.0f;
    float minFrameTime = std::numeric_limits<float>::max();

    for (const auto& frame : m_frameHistory) {
        avgFrameTime += frame.frameTime;
        avgInputTime += frame.inputTime;
        avgStreamingTime += frame.streamingTime;
        avgDecorationTime += frame.decorationTime;
        avgChunkProcessTime += frame.chunkProcessTime;
        avgRenderTime += frame.renderTime;

        avgPendingDecorations += frame.pendingDecorations;
        avgDecorationsInProgress += frame.decorationsInProgress;
        avgPendingLoads += frame.pendingLoads;
        avgCompletedChunks += frame.completedChunks;
        avgMeshQueue += frame.meshQueueSize;

        maxFrameTime = std::max(maxFrameTime, frame.frameTime);
        minFrameTime = std::min(minFrameTime, frame.frameTime);
    }

    size_t numFrames = m_frameHistory.size();
    avgFrameTime /= numFrames;
    avgInputTime /= numFrames;
    avgStreamingTime /= numFrames;
    avgDecorationTime /= numFrames;
    avgChunkProcessTime /= numFrames;
    avgRenderTime /= numFrames;

    avgPendingDecorations /= numFrames;
    avgDecorationsInProgress /= numFrames;
    avgPendingLoads /= numFrames;
    avgCompletedChunks /= numFrames;
    avgMeshQueue /= numFrames;

    float avgFPS = (avgFrameTime > 0.0f) ? 1000.0f / avgFrameTime : 0.0f;

    // Get current frame data
    const PerfFrameData& current = m_currentFrame;

    std::cout << "\n============================== Performance Report ==============================\n";
    std::cout << std::fixed << std::setprecision(2);

    // Player position
    std::cout << "Player: (" << current.playerPosition.x << ", " << current.playerPosition.y
              << ", " << current.playerPosition.z << ")\n";
    std::cout << "Distance from spawn: " << current.distanceFromSpawn << " blocks\n\n";

    // Frame timing
    std::cout << "--- Frame Timing (averaged over " << numFrames << " frames) ---\n";
    std::cout << "FPS:            " << avgFPS << " (" << minFrameTime << " - " << maxFrameTime << " ms)\n";
    std::cout << "Frame Time:     " << avgFrameTime << " ms (100.0%)\n";
    std::cout << "  Input:        " << avgInputTime << " ms (" << (avgInputTime/avgFrameTime*100.0f) << "%)\n";
    std::cout << "  Streaming:    " << avgStreamingTime << " ms (" << (avgStreamingTime/avgFrameTime*100.0f) << "%)\n";
    std::cout << "  Decoration:   " << avgDecorationTime << " ms (" << (avgDecorationTime/avgFrameTime*100.0f) << "%)\n";
    std::cout << "  Chunk Upload: " << avgChunkProcessTime << " ms (" << (avgChunkProcessTime/avgFrameTime*100.0f) << "%)\n";
    std::cout << "  Render:       " << avgRenderTime << " ms (" << (avgRenderTime/avgFrameTime*100.0f) << "%)\n";
    std::cout << "  Unaccounted:  " << (avgFrameTime - avgInputTime - avgStreamingTime - avgDecorationTime - avgChunkProcessTime - avgRenderTime)
              << " ms\n\n";

    // Queue sizes
    std::cout << "--- Queue Sizes (current frame) ---\n";
    std::cout << "Pending Decorations:        " << current.pendingDecorations
              << " (avg: " << avgPendingDecorations << ")\n";
    std::cout << "Decorations In Progress:    " << current.decorationsInProgress
              << " (avg: " << avgDecorationsInProgress << ")\n";
    std::cout << "Pending Chunk Loads:        " << current.pendingLoads
              << " (avg: " << avgPendingLoads << ")\n";
    std::cout << "Completed Chunks:           " << current.completedChunks
              << " (avg: " << avgCompletedChunks << ")\n";
    std::cout << "Mesh Generation Queue:      " << current.meshQueueSize
              << " (avg: " << avgMeshQueue << ")\n";

    // Bottleneck analysis
    std::cout << "\n--- Bottleneck Analysis ---\n";
    if (current.pendingDecorations > 20) {
        std::cout << "WARNING: High decoration backlog (" << current.pendingDecorations << " chunks)\n";
        std::cout << "  - Consider increasing MAX_CONCURRENT_DECORATIONS\n";
        std::cout << "  - Consider processing more decorations per frame\n";
    }
    if (current.meshQueueSize > 10) {
        std::cout << "WARNING: High mesh generation backlog (" << current.meshQueueSize << " chunks)\n";
        std::cout << "  - Consider increasing mesh worker threads\n";
    }
    if (current.completedChunks > 10) {
        std::cout << "WARNING: High GPU upload backlog (" << current.completedChunks << " chunks)\n";
        std::cout << "  - Consider processing more chunks per frame\n";
        std::cout << "  - GPU may be bottlenecked\n";
    }
    if (avgDecorationTime > 5.0f) {
        std::cout << "WARNING: Decoration processing taking " << avgDecorationTime << " ms/frame\n";
        std::cout << "  - This is a significant bottleneck\n";
    }
    if (avgChunkProcessTime > 5.0f) {
        std::cout << "WARNING: Chunk upload processing taking " << avgChunkProcessTime << " ms/frame\n";
        std::cout << "  - GPU upload may be bottlenecked\n";
    }

    std::cout << "================================================================================\n\n";
}

void PerformanceMonitor::setReportInterval(float seconds) {
    m_reportInterval = seconds;
}

bool PerformanceMonitor::shouldPrintReport() const {
    return m_timeSinceLastReport >= m_reportInterval;
}

float PerformanceMonitor::getAverageFPS() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameHistory.empty()) return 0.0f;

    float avgFrameTime = 0.0f;
    for (const auto& frame : m_frameHistory) {
        avgFrameTime += frame.frameTime;
    }
    avgFrameTime /= m_frameHistory.size();

    return (avgFrameTime > 0.0f) ? 1000.0f / avgFrameTime : 0.0f;
}

float PerformanceMonitor::getAverageFrameTime() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameHistory.empty()) return 0.0f;

    float total = 0.0f;
    for (const auto& frame : m_frameHistory) {
        total += frame.frameTime;
    }
    return total / m_frameHistory.size();
}

float PerformanceMonitor::getWorstFrameTime() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameHistory.empty()) return 0.0f;

    float worst = 0.0f;
    for (const auto& frame : m_frameHistory) {
        worst = std::max(worst, frame.frameTime);
    }
    return worst;
}

void PerformanceMonitor::resetFrameData() {
    m_currentFrame = PerfFrameData{};
}

// archiveFrameData() removed - logic inlined into endFrame() to avoid deadlock
