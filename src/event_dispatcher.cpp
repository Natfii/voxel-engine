#include "event_dispatcher.h"
#include <algorithm>
#include <chrono>

// Singleton instance
EventDispatcher& EventDispatcher::instance() {
    static EventDispatcher instance;
    return instance;
}

// Constructor
EventDispatcher::EventDispatcher()
    : m_running(false)
    , m_nextHandle(1)
    , m_eventsProcessed(0)
    , m_eventsCancelled(0)
{
}

// Destructor
EventDispatcher::~EventDispatcher() {
    if (m_running.load()) {
        stop();
    }
}

// Start the event handler thread
void EventDispatcher::start() {
    if (m_running.load()) {
        return; // Already running
    }

    m_running.store(true);
    m_handlerThread = std::thread(&EventDispatcher::eventHandlerThread, this);
}

// Stop the event handler thread
void EventDispatcher::stop() {
    if (!m_running.load()) {
        return; // Not running
    }

    m_running.store(false);

    // Wake up the handler thread
    m_queueCV.notify_one();

    // Wait for the handler thread to finish
    if (m_handlerThread.joinable()) {
        m_handlerThread.join();
    }
}

// Check if dispatcher is running
bool EventDispatcher::isRunning() const {
    return m_running.load();
}

// Subscribe to events
ListenerHandle EventDispatcher::subscribe(EventType type, EventCallback callback,
                                         EventPriority priority,
                                         const std::string& owner) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    EventListener listener;
    listener.handle = generateHandle();
    listener.type = type;
    listener.priority = priority;
    listener.callback = std::move(callback);
    listener.owner = owner;

    m_listeners[type].push_back(std::move(listener));

    // Sort listeners by priority (highest first)
    std::sort(m_listeners[type].begin(), m_listeners[type].end(),
        [](const EventListener& a, const EventListener& b) {
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        });

    return listener.handle;
}

// Subscribe with filter
ListenerHandle EventDispatcher::subscribeFiltered(EventType type, EventCallback callback,
                                                  EventFilter filter,
                                                  EventPriority priority,
                                                  const std::string& owner) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    FilteredListener filteredListener;
    filteredListener.listener.handle = generateHandle();
    filteredListener.listener.type = type;
    filteredListener.listener.priority = priority;
    filteredListener.listener.callback = std::move(callback);
    filteredListener.listener.owner = owner;
    filteredListener.filter = std::move(filter);

    m_filteredListeners[type].push_back(std::move(filteredListener));

    // Sort filtered listeners by priority (highest first)
    std::sort(m_filteredListeners[type].begin(), m_filteredListeners[type].end(),
        [](const FilteredListener& a, const FilteredListener& b) {
            return static_cast<int>(a.listener.priority) > static_cast<int>(b.listener.priority);
        });

    return filteredListener.listener.handle;
}

// Unsubscribe by handle
void EventDispatcher::unsubscribe(ListenerHandle handle) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    // Search in regular listeners
    for (auto& pair : m_listeners) {
        auto& listeners = pair.second;
        auto it = std::remove_if(listeners.begin(), listeners.end(),
            [handle](const EventListener& listener) {
                return listener.handle == handle;
            });

        if (it != listeners.end()) {
            listeners.erase(it, listeners.end());
            return;
        }
    }

    // Search in filtered listeners
    for (auto& pair : m_filteredListeners) {
        auto& listeners = pair.second;
        auto it = std::remove_if(listeners.begin(), listeners.end(),
            [handle](const FilteredListener& listener) {
                return listener.listener.handle == handle;
            });

        if (it != listeners.end()) {
            listeners.erase(it, listeners.end());
            return;
        }
    }
}

// Unsubscribe all by owner
void EventDispatcher::unsubscribeAll(const std::string& owner) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    // Remove from regular listeners
    for (auto& pair : m_listeners) {
        auto& listeners = pair.second;
        auto it = std::remove_if(listeners.begin(), listeners.end(),
            [&owner](const EventListener& listener) {
                return listener.owner == owner;
            });

        if (it != listeners.end()) {
            listeners.erase(it, listeners.end());
        }
    }

    // Remove from filtered listeners
    for (auto& pair : m_filteredListeners) {
        auto& listeners = pair.second;
        auto it = std::remove_if(listeners.begin(), listeners.end(),
            [&owner](const FilteredListener& listener) {
                return listener.listener.owner == owner;
            });

        if (it != listeners.end()) {
            listeners.erase(it, listeners.end());
        }
    }
}

// Unsubscribe all by event type
void EventDispatcher::unsubscribeAll(EventType type) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    // Clear regular listeners
    auto it = m_listeners.find(type);
    if (it != m_listeners.end()) {
        it->second.clear();
    }

    // Clear filtered listeners
    auto fit = m_filteredListeners.find(type);
    if (fit != m_filteredListeners.end()) {
        fit->second.clear();
    }
}

// Dispatch event asynchronously
void EventDispatcher::dispatch(std::unique_ptr<Event> event) {
    if (!event) {
        return;
    }

    // Set timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    event->timestamp = duration.count() / 1000.0;

    // Add to queue
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_eventQueue.push(std::move(event));
    }

    // Notify handler thread
    m_queueCV.notify_one();
}

// Dispatch event synchronously
void EventDispatcher::dispatchImmediate(Event& event) {
    // Set timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    event.timestamp = duration.count() / 1000.0;

    // Process immediately on calling thread
    processEvent(event);
}

// Queue event for main thread
void EventDispatcher::queueForMainThread(std::unique_ptr<Event> event) {
    if (!event) {
        return;
    }

    // Set timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    event->timestamp = duration.count() / 1000.0;

    // Add to main thread queue
    std::lock_guard<std::mutex> lock(m_mainThreadQueueMutex);
    m_mainThreadQueue.push(std::move(event));
}

// Process main thread queue
void EventDispatcher::processMainThreadQueue() {
    std::queue<std::unique_ptr<Event>> tempQueue;

    // Swap queues under lock to minimize lock time
    {
        std::lock_guard<std::mutex> lock(m_mainThreadQueueMutex);
        tempQueue.swap(m_mainThreadQueue);
    }

    // Process all events
    while (!tempQueue.empty()) {
        auto event = std::move(tempQueue.front());
        tempQueue.pop();

        if (event) {
            processEvent(*event);
        }
    }
}

// Get async queue size
size_t EventDispatcher::getQueueSize() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_eventQueue.size();
}

// Get listener count for event type
size_t EventDispatcher::getListenerCount(EventType type) const {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    size_t count = 0;

    auto it = m_listeners.find(type);
    if (it != m_listeners.end()) {
        count += it->second.size();
    }

    auto fit = m_filteredListeners.find(type);
    if (fit != m_filteredListeners.end()) {
        count += fit->second.size();
    }

    return count;
}

// Get total listener count
size_t EventDispatcher::getTotalListenerCount() const {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    size_t count = 0;

    for (const auto& pair : m_listeners) {
        count += pair.second.size();
    }

    for (const auto& pair : m_filteredListeners) {
        count += pair.second.size();
    }

    return count;
}

// Event handler thread main loop
void EventDispatcher::eventHandlerThread() {
    while (m_running.load()) {
        std::unique_ptr<Event> event;

        // Wait for event or stop signal
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait(lock, [this] {
                return !m_eventQueue.empty() || !m_running.load();
            });

            // Check if we should stop
            if (!m_running.load() && m_eventQueue.empty()) {
                break;
            }

            // Get next event
            if (!m_eventQueue.empty()) {
                event = std::move(m_eventQueue.front());
                m_eventQueue.pop();
            }
        }

        // Process event if we got one
        if (event) {
            processEvent(*event);
            m_eventsProcessed.fetch_add(1);
        }
    }

    // Process remaining events before shutting down
    std::queue<std::unique_ptr<Event>> remainingEvents;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        remainingEvents.swap(m_eventQueue);
    }

    while (!remainingEvents.empty()) {
        auto event = std::move(remainingEvents.front());
        remainingEvents.pop();

        if (event) {
            processEvent(*event);
            m_eventsProcessed.fetch_add(1);
        }
    }
}

// Process a single event
void EventDispatcher::processEvent(Event& event) {
    EventType type = event.type;

    // Snapshot listeners so callbacks don't run while holding the mutex.
    std::vector<EventListener> listenersCopy;
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        auto it = m_listeners.find(type);
        if (it != m_listeners.end()) {
            listenersCopy = it->second;
        }
    }

    for (const auto& listener : listenersCopy) {
        // Monitor priority always runs, others stop if cancelled
        if (listener.priority != EventPriority::MONITOR && event.isCancelled()) {
            break;
        }

        try {
            // Call the callback
            listener.callback(event);
        } catch (const std::exception& e) {
            // Log error but continue processing other listeners
            // In a real implementation, you'd use your logging system here
            // For now, we'll silently catch to prevent crashes
        } catch (...) {
            // Catch all other exceptions
        }
    }

    // Process filtered listeners
    auto fit = m_filteredListeners.find(type);
    if (fit != m_filteredListeners.end()) {
        for (const auto& filteredListener : fit->second) {
            // Monitor priority always runs, others stop if cancelled
            if (filteredListener.listener.priority != EventPriority::MONITOR &&
                event.isCancelled()) {
                break;
            }

            // Check filter
            try {
                if (!filteredListener.filter(event)) {
                    continue; // Filter rejected the event
                }
            } catch (...) {
                continue; // Filter threw exception, skip this listener
            }

            try {
                // Call the callback
                filteredListener.listener.callback(event);
            } catch (const std::exception& e) {
                // Log error but continue processing other listeners
            } catch (...) {
                // Catch all other exceptions
            }
        }
    }

    // Update statistics
    if (event.isCancelled()) {
        m_eventsCancelled.fetch_add(1);
    }
}

// Generate unique listener handle
ListenerHandle EventDispatcher::generateHandle() {
    return m_nextHandle.fetch_add(1);
}
