/**
 * @file event_system_example.cpp
 * @brief Example usage of the thread-safe EventDispatcher
 *
 * This example demonstrates how to use the event system in the voxel engine.
 * Compile this example with:
 *   g++ -std=c++17 -I../include event_system_example.cpp ../src/event_types.cpp ../src/event_dispatcher.cpp -pthread -o event_test
 */

#include "event_dispatcher.h"
#include <iostream>
#include <thread>
#include <chrono>

// Example 1: Basic event subscription and dispatch
void example_basic_usage() {
    std::cout << "\n=== Example 1: Basic Usage ===" << std::endl;

    auto& dispatcher = EventDispatcher::instance();
    dispatcher.start();

    // Subscribe to block place events
    auto handle = dispatcher.subscribe(
        EventType::BLOCK_PLACE,
        [](Event& e) {
            auto& blockEvent = static_cast<BlockPlaceEvent&>(e);
            std::cout << "Block placed at position ("
                     << blockEvent.position.x << ", "
                     << blockEvent.position.y << ", "
                     << blockEvent.position.z << ")"
                     << " with ID: " << blockEvent.blockID << std::endl;
        },
        EventPriority::NORMAL,
        "example_listener"
    );

    // Dispatch a block place event
    dispatcher.dispatch(std::make_unique<BlockPlaceEvent>(
        glm::ivec3(10, 20, 30),  // position
        1,                        // block ID (e.g., dirt)
        0,                        // placer entity ID
        glm::ivec3(10, 19, 30)   // placed against
    ));

    // Give the handler thread time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    dispatcher.unsubscribe(handle);
}

// Example 2: Priority-based event handling
void example_priority_handling() {
    std::cout << "\n=== Example 2: Priority-Based Handling ===" << std::endl;

    auto& dispatcher = EventDispatcher::instance();

    // Subscribe with different priorities
    dispatcher.subscribe(
        EventType::BLOCK_BREAK,
        [](Event& e) {
            std::cout << "  [HIGHEST] First handler (might cancel event)" << std::endl;
            e.cancel();  // Cancel the event
        },
        EventPriority::HIGHEST,
        "protection_system"
    );

    dispatcher.subscribe(
        EventType::BLOCK_BREAK,
        [](Event& e) {
            if (e.isCancelled()) {
                std::cout << "  [NORMAL] Event was cancelled, but I can still see it!" << std::endl;
            } else {
                std::cout << "  [NORMAL] Normal handler executing" << std::endl;
            }
        },
        EventPriority::NORMAL,
        "normal_handler"
    );

    dispatcher.subscribe(
        EventType::BLOCK_BREAK,
        [](Event& e) {
            std::cout << "  [MONITOR] Monitor always runs (for logging)" << std::endl;
            std::cout << "  [MONITOR] Event cancelled: " << (e.isCancelled() ? "yes" : "no") << std::endl;
        },
        EventPriority::MONITOR,
        "logger"
    );

    // Dispatch the event
    dispatcher.dispatch(std::make_unique<BlockBreakEvent>(
        glm::ivec3(5, 10, 15),
        2,  // block ID
        BreakCause::PLAYER,
        0   // breaker entity ID
    ));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    dispatcher.unsubscribeAll(EventType::BLOCK_BREAK);
}

// Example 3: Filtered event subscription
void example_filtered_subscription() {
    std::cout << "\n=== Example 3: Filtered Subscription ===" << std::endl;

    auto& dispatcher = EventDispatcher::instance();

    // Only handle block breaks caused by players
    dispatcher.subscribeFiltered(
        EventType::BLOCK_BREAK,
        [](Event& e) {
            auto& blockEvent = static_cast<BlockBreakEvent&>(e);
            std::cout << "  Player broke a block at ("
                     << blockEvent.position.x << ", "
                     << blockEvent.position.y << ", "
                     << blockEvent.position.z << ")" << std::endl;
        },
        [](const Event& e) {
            const auto& blockEvent = static_cast<const BlockBreakEvent&>(e);
            return blockEvent.cause == BreakCause::PLAYER;
        },
        EventPriority::NORMAL,
        "player_break_handler"
    );

    // Dispatch events with different causes
    std::cout << "Breaking block with PLAYER cause:" << std::endl;
    dispatcher.dispatch(std::make_unique<BlockBreakEvent>(
        glm::ivec3(1, 2, 3),
        1,
        BreakCause::PLAYER,
        0
    ));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Breaking block with WATER cause (filtered out):" << std::endl;
    dispatcher.dispatch(std::make_unique<BlockBreakEvent>(
        glm::ivec3(4, 5, 6),
        1,
        BreakCause::WATER,
        -1
    ));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    dispatcher.unsubscribeAll("player_break_handler");
}

// Example 4: Immediate (synchronous) dispatch
void example_immediate_dispatch() {
    std::cout << "\n=== Example 4: Immediate Dispatch ===" << std::endl;

    auto& dispatcher = EventDispatcher::instance();

    dispatcher.subscribe(
        EventType::PLAYER_JUMP,
        [](Event& e) {
            auto& jumpEvent = static_cast<PlayerJumpEvent&>(e);
            std::cout << "  Player jumped at ("
                     << jumpEvent.position.x << ", "
                     << jumpEvent.position.y << ", "
                     << jumpEvent.position.z << ")" << std::endl;
        },
        EventPriority::NORMAL,
        "jump_handler"
    );

    std::cout << "Before immediate dispatch" << std::endl;

    PlayerJumpEvent jumpEvent(glm::vec3(10.5f, 64.0f, 20.5f), 0);
    dispatcher.dispatchImmediate(jumpEvent);

    std::cout << "After immediate dispatch (processed synchronously)" << std::endl;

    dispatcher.unsubscribeAll("jump_handler");
}

// Example 5: Main thread queue (for GPU operations)
void example_main_thread_queue() {
    std::cout << "\n=== Example 5: Main Thread Queue ===" << std::endl;

    auto& dispatcher = EventDispatcher::instance();

    dispatcher.subscribe(
        EventType::CHUNK_LOAD,
        [](Event& e) {
            auto& chunkEvent = static_cast<ChunkLoadEvent&>(e);
            std::cout << "  Chunk loaded: ("
                     << chunkEvent.chunkX << ", "
                     << chunkEvent.chunkY << ", "
                     << chunkEvent.chunkZ << ") "
                     << (chunkEvent.isNewChunk ? "[NEW]" : "[FROM DISK]")
                     << std::endl;
        },
        EventPriority::NORMAL,
        "chunk_loader"
    );

    // Queue event for main thread processing
    std::cout << "Queueing chunk load event for main thread..." << std::endl;
    dispatcher.queueForMainThread(std::make_unique<ChunkLoadEvent>(
        0, 0, 0, true  // New chunk at origin
    ));

    // In a real game loop, you'd call this every frame
    std::cout << "Processing main thread queue..." << std::endl;
    dispatcher.processMainThreadQueue();

    dispatcher.unsubscribeAll("chunk_loader");
}

// Example 6: Statistics and monitoring
void example_statistics() {
    std::cout << "\n=== Example 6: Statistics ===" << std::endl;

    auto& dispatcher = EventDispatcher::instance();

    std::cout << "Total listeners: " << dispatcher.getTotalListenerCount() << std::endl;
    std::cout << "Queue size: " << dispatcher.getQueueSize() << std::endl;
    std::cout << "Events processed: " << dispatcher.getEventsProcessed() << std::endl;
    std::cout << "Events cancelled: " << dispatcher.getEventsCancelled() << std::endl;
}

int main() {
    std::cout << "=== EventDispatcher Example Program ===" << std::endl;

    try {
        example_basic_usage();
        example_priority_handling();
        example_filtered_subscription();
        example_immediate_dispatch();
        example_main_thread_queue();
        example_statistics();

        std::cout << "\n=== All examples completed successfully ===" << std::endl;

        // Stop the dispatcher
        EventDispatcher::instance().stop();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
