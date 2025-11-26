#pragma once

#include "event_types.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

/**
 * Thread-Safe Event Dispatcher for Voxel Engine
 *
 * This dispatcher implements a producer-consumer pattern with priority-based
 * event handling. It supports both asynchronous (queued) and synchronous
 * (immediate) event dispatch, with separate queues for main-thread operations.
 *
 * Features:
 * - Thread-safe event queuing and dispatch
 * - Priority-based listener ordering
 * - Event cancellation support
 * - Filtered event listeners
 * - Separate main-thread queue for GPU operations
 * - Event monitoring without cancellation
 *
 * Usage:
 *   auto& dispatcher = EventDispatcher::instance();
 *   dispatcher.start();
 *
 *   // Subscribe to events
 *   auto handle = dispatcher.subscribe(EventType::BLOCK_PLACED,
 *       [](Event& e) {
 *           auto& blockEvent = static_cast<BlockEvent&>(e);
 *           // Handle event...
 *           return true; // Event handled
 *       },
 *       EventPriority::NORMAL,
 *       "block_system"
 *   );
 *
 *   // Dispatch events
 *   dispatcher.dispatch(std::make_unique<BlockEvent>(...));
 *
 *   // Clean up
 *   dispatcher.unsubscribe(handle);
 *   dispatcher.stop();
 */

// Listener handle for unsubscribing
using ListenerHandle = uint64_t;

/**
 * Event priority levels
 * Higher priority listeners are called first
 */
enum class EventPriority {
    LOWEST = 0,   // Called last
    LOW = 1,      // Low priority
    NORMAL = 2,   // Default priority
    HIGH = 3,     // High priority
    HIGHEST = 4,  // Called first
    MONITOR = 5   // For logging/monitoring only, cannot cancel, always called
};

/**
 * Listener registration info
 * Stores information about a registered event listener
 */
struct EventListener {
    ListenerHandle handle;
    EventType type;
    EventPriority priority;
    EventCallback callback;
    std::string owner;  // For debugging, e.g., "block:grass" or "script:mymod"
};

/**
 * EventDispatcher - Thread-safe event system
 *
 * Singleton class that manages event dispatching and listener registration.
 * Events can be dispatched asynchronously (queued) or synchronously (immediate).
 * A separate handler thread processes the async queue.
 */
class EventDispatcher {
public:
    /**
     * Get the singleton instance
     */
    static EventDispatcher& instance();

    /**
     * Start the event handler thread
     * Must be called before dispatching events
     */
    void start();

    /**
     * Stop the event handler thread
     * Processes remaining queued events before stopping
     */
    void stop();

    /**
     * Check if the dispatcher is running
     */
    bool isRunning() const;

    /**
     * Subscribe to events
     *
     * @param type Event type to listen for
     * @param callback Function to call when event occurs
     * @param priority Priority level (higher = called first)
     * @param owner Optional owner identifier for debugging
     * @return Handle to use for unsubscribing
     */
    ListenerHandle subscribe(EventType type, EventCallback callback,
                            EventPriority priority = EventPriority::NORMAL,
                            const std::string& owner = "");

    /**
     * Subscribe with filter (only receives events matching filter)
     *
     * @param type Event type to listen for
     * @param callback Function to call when event occurs
     * @param filter Filter function to determine if event should be processed
     * @param priority Priority level (higher = called first)
     * @param owner Optional owner identifier for debugging
     * @return Handle to use for unsubscribing
     */
    ListenerHandle subscribeFiltered(EventType type, EventCallback callback,
                                     EventFilter filter,
                                     EventPriority priority = EventPriority::NORMAL,
                                     const std::string& owner = "");

    /**
     * Unsubscribe a specific listener
     *
     * @param handle Handle returned by subscribe()
     */
    void unsubscribe(ListenerHandle handle);

    /**
     * Unsubscribe all listeners for a specific owner
     *
     * @param owner Owner identifier used when subscribing
     */
    void unsubscribeAll(const std::string& owner);

    /**
     * Unsubscribe all listeners for a specific event type
     *
     * @param type Event type to unsubscribe from
     */
    void unsubscribeAll(EventType type);

    /**
     * Dispatch event asynchronously (queued)
     * Event will be processed on the handler thread
     *
     * @param event Event to dispatch (ownership transferred)
     */
    void dispatch(std::unique_ptr<Event> event);

    /**
     * Dispatch event synchronously (immediate)
     * Event is processed on the calling thread immediately
     *
     * @param event Event to dispatch (not moved, can be reused)
     */
    void dispatchImmediate(Event& event);

    /**
     * Queue an event to be processed on main thread
     * Use this for GPU operations or other main-thread-only operations
     *
     * @param event Event to queue (ownership transferred)
     */
    void queueForMainThread(std::unique_ptr<Event> event);

    /**
     * Process main thread queue
     * Must be called from the main thread each frame
     */
    void processMainThreadQueue();

    /**
     * Get current async queue size
     */
    size_t getQueueSize() const;

    /**
     * Get number of listeners for a specific event type
     */
    size_t getListenerCount(EventType type) const;

    /**
     * Get total number of registered listeners
     */
    size_t getTotalListenerCount() const;

    /**
     * Get total events processed
     */
    uint64_t getEventsProcessed() const { return m_eventsProcessed.load(); }

    /**
     * Get total events cancelled
     */
    uint64_t getEventsCancelled() const { return m_eventsCancelled.load(); }

private:
    EventDispatcher();
    ~EventDispatcher();
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    /**
     * Event handler thread main loop
     */
    void eventHandlerThread();

    /**
     * Process a single event (calls all registered listeners)
     */
    void processEvent(Event& event);

    /**
     * Generate unique listener handle
     */
    ListenerHandle generateHandle();

    // Thread-safe event queue
    std::queue<std::unique_ptr<Event>> m_eventQueue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    // Main thread queue (for GPU operations)
    std::queue<std::unique_ptr<Event>> m_mainThreadQueue;
    mutable std::mutex m_mainThreadQueueMutex;

    // Listeners organized by event type, then priority
    std::unordered_map<EventType, std::vector<EventListener>> m_listeners;
    mutable std::mutex m_listenersMutex;

    // Filtered listeners
    struct FilteredListener {
        EventListener listener;
        EventFilter filter;
    };
    std::unordered_map<EventType, std::vector<FilteredListener>> m_filteredListeners;

    // Thread control
    std::thread m_handlerThread;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_nextHandle{1};

    // Statistics
    std::atomic<uint64_t> m_eventsProcessed{0};
    std::atomic<uint64_t> m_eventsCancelled{0};
};
