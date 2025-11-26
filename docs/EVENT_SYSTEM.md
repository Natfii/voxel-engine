# Event System Documentation

## Overview

The voxel engine uses a thread-safe, priority-based event system for handling game events. The system is designed to be efficient, flexible, and easy to use, drawing inspiration from event systems like Minecraft Forge and eventpp.

## Architecture

### Core Components

1. **Event Types** (`event_types.h` / `event_types.cpp`)
   - Base `Event` struct and derived event classes
   - Event type enumeration
   - Event callback and filter type definitions

2. **Event Dispatcher** (`event_dispatcher.h` / `event_dispatcher.cpp`)
   - Thread-safe singleton dispatcher
   - Priority-based event handling
   - Support for async (queued) and sync (immediate) dispatch
   - Separate main-thread queue for GPU operations

### Thread Safety

The EventDispatcher is fully thread-safe and uses:
- **Mutexes** for protecting shared data structures
- **Condition variables** for efficient thread synchronization
- **Atomic operations** for statistics tracking

Events can be dispatched from any thread, and the dispatcher will handle them safely.

## Event Types

### Block Events
- `BLOCK_BREAK` - Fired when a block is broken (cancellable)
- `BLOCK_PLACE` - Fired when a block is placed (cancellable)
- `BLOCK_INTERACT` - Fired when a player interacts with a block (cancellable)
- `BLOCK_STEP` - Fired when an entity steps on a block
- `BLOCK_UPDATE` - Fired when a block receives an update tick

### Neighbor Events
- `NEIGHBOR_CHANGED` - Fired when an adjacent block changes

### Chunk Events
- `CHUNK_LOAD` - Fired when a chunk is loaded
- `CHUNK_UNLOAD` - Fired when a chunk is unloaded

### Player Events
- `PLAYER_MOVE` - Fired when a player moves (cancellable)
- `PLAYER_JUMP` - Fired when a player jumps
- `PLAYER_LAND` - Fired when a player lands after falling
- `PLAYER_SWIM` - Fired when a player is swimming

### Time Events
- `TIME_CHANGE` - Fired when world time changes
- `DAY_START` - Fired at sunrise
- `NIGHT_START` - Fired at sunset

### Custom Events
- `CUSTOM` - For user-defined events

## Event Priorities

Events are processed in priority order (highest first):

1. **HIGHEST** (4) - Critical handlers, security checks
2. **HIGH** (3) - Important game logic
3. **NORMAL** (2) - Default priority
4. **LOW** (1) - Non-critical handlers
5. **LOWEST** (0) - Last resort handlers
6. **MONITOR** (5) - Special priority for logging/monitoring
   - Always executed, even if event is cancelled
   - Cannot cancel events
   - Use for analytics, logging, debugging

## Usage

### Starting the Dispatcher

```cpp
#include "event_dispatcher.h"

// Get singleton instance
auto& dispatcher = EventDispatcher::instance();

// Start the event handler thread
dispatcher.start();
```

### Subscribing to Events

#### Basic Subscription

```cpp
ListenerHandle handle = dispatcher.subscribe(
    EventType::BLOCK_PLACE,
    [](Event& e) {
        auto& blockEvent = static_cast<BlockPlaceEvent&>(e);
        // Handle the event...
    },
    EventPriority::NORMAL,
    "my_system"  // Owner identifier for debugging
);
```

#### Filtered Subscription

```cpp
// Only handle blocks broken by players
dispatcher.subscribeFiltered(
    EventType::BLOCK_BREAK,
    [](Event& e) {
        auto& blockEvent = static_cast<BlockBreakEvent&>(e);
        // Handle player-caused breaks...
    },
    [](const Event& e) {
        const auto& blockEvent = static_cast<const BlockBreakEvent&>(e);
        return blockEvent.cause == BreakCause::PLAYER;
    },
    EventPriority::NORMAL,
    "player_break_handler"
);
```

### Dispatching Events

#### Asynchronous Dispatch (Recommended)

Events are queued and processed on a background thread:

```cpp
dispatcher.dispatch(std::make_unique<BlockPlaceEvent>(
    glm::ivec3(10, 20, 30),  // position
    1,                        // block ID
    0,                        // placer entity ID
    glm::ivec3(10, 19, 30)   // placed against
));
```

#### Synchronous Dispatch (Immediate)

Events are processed immediately on the calling thread:

```cpp
PlayerJumpEvent jumpEvent(glm::vec3(10.5f, 64.0f, 20.5f), 0);
dispatcher.dispatchImmediate(jumpEvent);
```

#### Main Thread Dispatch

For GPU operations that must run on the main thread:

```cpp
// From any thread:
dispatcher.queueForMainThread(std::make_unique<ChunkLoadEvent>(
    0, 0, 0, true
));

// In your main game loop:
dispatcher.processMainThreadQueue();
```

### Unsubscribing

```cpp
// Unsubscribe a specific listener
dispatcher.unsubscribe(handle);

// Unsubscribe all listeners for an owner
dispatcher.unsubscribeAll("my_system");

// Unsubscribe all listeners for an event type
dispatcher.unsubscribeAll(EventType::BLOCK_PLACE);
```

### Event Cancellation

Some events can be cancelled to prevent their default behavior:

```cpp
dispatcher.subscribe(
    EventType::BLOCK_BREAK,
    [](Event& e) {
        auto& blockEvent = static_cast<BlockBreakEvent&>(e);

        // Check if this is a protected block
        if (isProtected(blockEvent.position)) {
            e.cancel();  // Prevent the block from breaking
        }
    },
    EventPriority::HIGHEST,
    "protection_system"
);
```

**Important**: Once an event is cancelled, lower-priority handlers won't run (except MONITOR priority).

### Statistics

```cpp
size_t queueSize = dispatcher.getQueueSize();
size_t listenerCount = dispatcher.getListenerCount(EventType::BLOCK_PLACE);
size_t totalListeners = dispatcher.getTotalListenerCount();
uint64_t eventsProcessed = dispatcher.getEventsProcessed();
uint64_t eventsCancelled = dispatcher.getEventsCancelled();
```

## Best Practices

### 1. Use Appropriate Priorities

- **HIGHEST**: Security checks, protection systems
- **HIGH**: Core game logic that others depend on
- **NORMAL**: Most game systems (default)
- **LOW**: Optional features, cosmetic effects
- **LOWEST**: Fallback handlers
- **MONITOR**: Logging, analytics, debugging only

### 2. Keep Handlers Fast

Event handlers should execute quickly to avoid blocking the event queue. For expensive operations:

```cpp
// DON'T do this:
dispatcher.subscribe(EventType::CHUNK_LOAD, [](Event& e) {
    expensiveOperation();  // Blocks event processing!
});

// DO this instead:
dispatcher.subscribe(EventType::CHUNK_LOAD, [](Event& e) {
    std::async(std::launch::async, []() {
        expensiveOperation();
    });
});
```

### 3. Use Owner Identifiers

Always provide meaningful owner identifiers for debugging:

```cpp
dispatcher.subscribe(
    EventType::BLOCK_PLACE,
    callback,
    EventPriority::NORMAL,
    "water_simulation"  // Clear identifier
);
```

### 4. Unsubscribe When Done

Clean up listeners when they're no longer needed:

```cpp
class MySystem {
    ListenerHandle m_handle;

public:
    void init() {
        auto& dispatcher = EventDispatcher::instance();
        m_handle = dispatcher.subscribe(...);
    }

    ~MySystem() {
        EventDispatcher::instance().unsubscribe(m_handle);
    }
};
```

### 5. Use Filters for Selective Processing

Instead of checking conditions in every handler:

```cpp
// DON'T do this:
dispatcher.subscribe(EventType::BLOCK_BREAK, [](Event& e) {
    auto& blockEvent = static_cast<BlockBreakEvent&>(e);
    if (blockEvent.cause != BreakCause::PLAYER) return;  // Wasteful
    // Handle event...
});

// DO this:
dispatcher.subscribeFiltered(
    EventType::BLOCK_BREAK,
    [](Event& e) { /* Handle event... */ },
    [](const Event& e) {
        return static_cast<const BlockBreakEvent&>(e).cause == BreakCause::PLAYER;
    }
);
```

### 6. Main Thread Queue for GPU Operations

Always use the main thread queue for operations that require OpenGL/Vulkan context:

```cpp
// Mesh update must happen on main thread
dispatcher.subscribe(EventType::CHUNK_MODIFIED, [](Event& e) {
    auto& chunkEvent = static_cast<ChunkEvent&>(e);

    // Queue mesh rebuild for main thread
    EventDispatcher::instance().queueForMainThread(
        std::make_unique<ChunkMeshRebuildEvent>(chunkEvent.chunkPos)
    );
});
```

## Performance Considerations

- **Async Dispatch**: O(1) to queue, O(n) to process (n = number of listeners)
- **Immediate Dispatch**: O(n) to process (n = number of listeners)
- **Filtering**: O(f) additional cost per filtered listener (f = filter complexity)
- **Memory**: Each listener ~64 bytes, events typically 32-128 bytes

The dispatcher uses a single background thread to process events sequentially, ensuring predictable behavior and avoiding race conditions.

## Shutdown

```cpp
// Stop the dispatcher (processes remaining events first)
dispatcher.stop();
```

## Example Code

See `docs/event_system_example.cpp` for complete working examples.

## Thread Safety Summary

| Operation | Thread-Safe | Notes |
|-----------|-------------|-------|
| `subscribe()` | Yes | Can be called from any thread |
| `subscribeFiltered()` | Yes | Can be called from any thread |
| `unsubscribe()` | Yes | Can be called from any thread |
| `dispatch()` | Yes | Can be called from any thread |
| `dispatchImmediate()` | Yes | Processes on calling thread |
| `queueForMainThread()` | Yes | Can be called from any thread |
| `processMainThreadQueue()` | No | Must be called from main thread |
| `getQueueSize()` | Yes | Thread-safe read |
| `getListenerCount()` | Yes | Thread-safe read |

## Future Enhancements

Potential improvements for future versions:

1. **Event Replay**: Record and replay events for debugging
2. **Event Profiling**: Track handler execution times
3. **Async Handlers**: Support for async/await style handlers
4. **Event Groups**: Subscribe to multiple event types at once
5. **Dynamic Event Types**: Runtime-defined event types for modding
6. **Network Events**: Built-in support for network event synchronization
