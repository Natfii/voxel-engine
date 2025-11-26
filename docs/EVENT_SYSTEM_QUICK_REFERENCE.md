# Event System Quick Reference

## Initialization

```cpp
auto& dispatcher = EventDispatcher::instance();
dispatcher.start();
```

## Subscribe to Events

### Basic
```cpp
auto handle = dispatcher.subscribe(
    EventType::BLOCK_PLACE,
    [](Event& e) {
        auto& evt = static_cast<BlockPlaceEvent&>(e);
        // Handle event
    },
    EventPriority::NORMAL,
    "system_name"
);
```

### With Filter
```cpp
dispatcher.subscribeFiltered(
    EventType::BLOCK_BREAK,
    [](Event& e) { /* handler */ },
    [](const Event& e) { return /* condition */; },
    EventPriority::NORMAL,
    "system_name"
);
```

## Dispatch Events

### Async (Queued)
```cpp
dispatcher.dispatch(std::make_unique<BlockPlaceEvent>(
    glm::ivec3(x, y, z),  // position
    blockID,
    placerID,
    glm::ivec3(againstX, againstY, againstZ)
));
```

### Sync (Immediate)
```cpp
BlockPlaceEvent evt(...);
dispatcher.dispatchImmediate(evt);
```

### Main Thread Queue
```cpp
// From any thread:
dispatcher.queueForMainThread(std::make_unique<SomeEvent>(...));

// In main game loop:
dispatcher.processMainThreadQueue();
```

## Unsubscribe

```cpp
dispatcher.unsubscribe(handle);              // Specific listener
dispatcher.unsubscribeAll("system_name");    // All from owner
dispatcher.unsubscribeAll(EventType::BLOCK_PLACE);  // All for type
```

## Cancel Events

```cpp
dispatcher.subscribe(EventType::BLOCK_BREAK, [](Event& e) {
    if (shouldCancel) {
        e.cancel();
    }
}, EventPriority::HIGHEST, "protection");
```

## Event Types Cheat Sheet

| Event Type | Cancellable | Common Use Cases |
|------------|-------------|------------------|
| `BLOCK_PLACE` | Yes | Protection, validation |
| `BLOCK_BREAK` | Yes | Protection, drop items |
| `BLOCK_INTERACT` | Yes | Custom GUIs, actions |
| `BLOCK_STEP` | No | Pressure plates, damage |
| `BLOCK_UPDATE` | No | Crop growth, redstone |
| `NEIGHBOR_CHANGED` | No | Block reactions |
| `CHUNK_LOAD` | No | Initialize chunk data |
| `CHUNK_UNLOAD` | No | Save chunk data |
| `PLAYER_MOVE` | Yes | Region protection |
| `PLAYER_JUMP` | No | Modify jump height |
| `PLAYER_LAND` | No | Fall damage |
| `TIME_CHANGE` | No | Time-based systems |
| `CUSTOM` | Varies | Mod events |

## Priority Guidelines

- **HIGHEST**: Security, protection systems
- **HIGH**: Core game logic
- **NORMAL**: Most systems (default)
- **LOW**: Cosmetic effects
- **LOWEST**: Fallback handlers
- **MONITOR**: Logging only (can't cancel)

## Common Patterns

### Protection System
```cpp
dispatcher.subscribe(EventType::BLOCK_BREAK, [](Event& e) {
    auto& evt = static_cast<BlockBreakEvent&>(e);
    if (isProtected(evt.position)) {
        e.cancel();
    }
}, EventPriority::HIGHEST, "protection");
```

### Drop Items on Block Break
```cpp
dispatcher.subscribe(EventType::BLOCK_BREAK, [](Event& e) {
    if (e.isCancelled()) return;  // Respect cancellation

    auto& evt = static_cast<BlockBreakEvent&>(e);
    dropItems(evt.position, evt.blockID);
}, EventPriority::NORMAL, "item_drops");
```

### Neighbor Reaction
```cpp
dispatcher.subscribe(EventType::NEIGHBOR_CHANGED, [](Event& e) {
    auto& evt = static_cast<NeighborChangedEvent&>(e);
    checkAndUpdate(evt.position);
}, EventPriority::NORMAL, "block_updates");
```

### Statistics Tracking
```cpp
dispatcher.subscribe(EventType::BLOCK_PLACE, [](Event& e) {
    auto& evt = static_cast<BlockPlaceEvent&>(e);
    stats.incrementBlocksPlaced();
}, EventPriority::MONITOR, "statistics");
```

## Performance Tips

1. **Keep handlers fast** - Offload heavy work to async tasks
2. **Use filters** - Avoid checking conditions in every handler
3. **Use appropriate priority** - Don't set everything to HIGHEST
4. **Unsubscribe when done** - Clean up unused listeners
5. **Batch events** - Don't dispatch thousands per frame

## Thread Safety

✅ **Thread-Safe Operations**:
- `subscribe()`, `subscribeFiltered()`
- `unsubscribe()`, `unsubscribeAll()`
- `dispatch()`, `dispatchImmediate()`
- `queueForMainThread()`
- All getter methods

⚠️ **Main Thread Only**:
- `processMainThreadQueue()`

## Shutdown

```cpp
dispatcher.stop();  // Processes remaining events first
```

## Statistics

```cpp
dispatcher.getQueueSize();           // Current queue size
dispatcher.getListenerCount(type);   // Listeners for type
dispatcher.getTotalListenerCount();  // All listeners
dispatcher.getEventsProcessed();     // Total processed
dispatcher.getEventsCancelled();     // Total cancelled
```

## Example: Complete System Integration

```cpp
class WaterSimulation {
    ListenerHandle m_neighborHandle;
    ListenerHandle m_placeHandle;
    ListenerHandle m_breakHandle;

public:
    void init() {
        auto& dispatcher = EventDispatcher::instance();

        // React to neighbor changes
        m_neighborHandle = dispatcher.subscribe(
            EventType::NEIGHBOR_CHANGED,
            [this](Event& e) { this->onNeighborChanged(e); },
            EventPriority::NORMAL,
            "water_simulation"
        );

        // React to block placement
        m_placeHandle = dispatcher.subscribeFiltered(
            EventType::BLOCK_PLACE,
            [this](Event& e) { this->onWaterPlaced(e); },
            [](const Event& e) {
                auto& evt = static_cast<const BlockPlaceEvent&>(e);
                return evt.blockID == WATER_BLOCK_ID;
            },
            EventPriority::NORMAL,
            "water_simulation"
        );
    }

    void shutdown() {
        auto& dispatcher = EventDispatcher::instance();
        dispatcher.unsubscribeAll("water_simulation");
    }

private:
    void onNeighborChanged(Event& e) { /* ... */ }
    void onWaterPlaced(Event& e) { /* ... */ }
};
```

## Debugging

Check event flow with MONITOR priority:

```cpp
dispatcher.subscribe(EventType::BLOCK_BREAK, [](Event& e) {
    auto& evt = static_cast<BlockBreakEvent&>(e);
    std::cout << "Block broken at (" << evt.position.x << ", "
              << evt.position.y << ", " << evt.position.z << ") "
              << "cancelled=" << e.isCancelled() << std::endl;
}, EventPriority::MONITOR, "debug");
```
