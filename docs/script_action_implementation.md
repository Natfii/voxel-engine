# Script Action System Implementation

## Overview

This document describes the implementation of the YAML-based script action system for blocks and biomes in the voxel engine. This system allows content creators to define event-driven behaviors entirely in YAML without writing C++ code.

## Architecture

### Components

1. **script_action.h** - Header file defining:
   - `ActionType` enum - Types of actions that can be executed
   - `ScriptAction` struct - Individual action definition
   - `ScriptEventHandler` struct - Event handler containing multiple actions
   - Registration functions for connecting YAML scripts to the event system

2. **script_action.cpp** - Implementation file containing:
   - YAML parsing logic for actions and handlers
   - Action execution engine
   - Event dispatcher integration
   - Probability-based action execution

3. **block_system.cpp** (modified) - Block registry integration:
   - Added `parseAndRegisterEventHandlers()` helper function
   - Integrated event parsing during block loading
   - Automatic handler registration after block definition

### Event Flow

```
YAML File → Block Loading → Parse Events → Register Handlers → Event Fires → Execute Actions
```

1. Block YAML files are loaded by BlockRegistry
2. The "events" section is parsed by `parseAndRegisterEventHandlers()`
3. Event handlers are registered with EventDispatcher
4. When an event fires, the dispatcher calls registered callbacks
5. Callbacks filter by block ID and execute actions
6. Actions are executed with probability checks and offsets

## Features

### Supported Events

- `on_break` - When a block is broken
- `on_place` - When a block is placed
- `on_step` - When an entity steps on the block
- `on_neighbor_change` - When an adjacent block changes
- `on_interact` - When a player right-clicks the block
- `on_update` - When the block receives a scheduled update tick

### Supported Actions

1. **PLACE_BLOCK** - Places a block at a position
2. **BREAK_BLOCK** - Breaks a block at a position
3. **SPAWN_STRUCTURE** - Spawns a structure at a position
4. **SPAWN_PARTICLES** - Spawns particle effects
5. **PLAY_SOUND** - Plays a sound effect (stubbed for future)
6. **RUN_COMMAND** - Executes a console command
7. **SET_METADATA** - Sets block metadata
8. **TRIGGER_UPDATE** - Schedules a block update tick

### Action Parameters

All actions support:
- `offset`: Relative position [x, y, z] from event location
- `probability`: Chance to execute (0-100, default 100)

Action-specific parameters:
- `block`: Block name for PLACE_BLOCK
- `structure`: Structure name for SPAWN_STRUCTURE
- `particle`: Particle name for SPAWN_PARTICLES
- `sound`: Sound name for PLAY_SOUND
- `command`: Command string for RUN_COMMAND
- `metadata`: Key-value map for SET_METADATA

## YAML Format

### Basic Structure

```yaml
id: 50
name: "Block Name"
texture: "texture.png"
durability: 3
# ... other block properties ...

events:
  event_type:
    - type: action_type
      parameter: value
      offset: [x, y, z]
      probability: 100
```

### Complete Example

```yaml
id: 50
name: "Pressure Plate"
texture: "pressure_plate.png"
durability: 1
affected_by_gravity: false

events:
  on_step:
    - type: run_command
      command: "echo Stepped on pressure plate!"
    - type: spawn_particles
      particle: "redstone_dust"
      offset: [0, 1, 0]
      probability: 100

  on_neighbor_change:
    - type: break_block
      offset: [0, 0, 0]
      probability: 50

  on_break:
    - type: spawn_structure
      structure: "hidden_door"
      offset: [0, -1, 0]
      probability: 10
```

## Implementation Details

### Probability System

Actions use a random number generator to determine if they execute:

```cpp
static bool rollProbability(int probability) {
    if (probability >= 100) return true;
    if (probability <= 0) return false;

    std::uniform_int_distribution<int> dist(1, 100);
    return dist(g_rng) <= probability;
}
```

### Event Filtering

Event handlers filter events by block ID to ensure only relevant blocks respond:

```cpp
// Only execute if the event is for this specific block
if (eventBlockID == blockID) {
    executeHandler(handler, position);
}
```

### Handler Registration

Handlers are registered with unique owner strings for easy cleanup:

```cpp
std::string ownerStr = "block:" + std::to_string(blockID);
ListenerHandle handle = dispatcher.subscribe(
    eventType,
    callback,
    EventPriority::NORMAL,
    ownerStr
);
```

### Memory Management

- Listener handles are stored in a global map for cleanup
- Handlers can be unregistered by calling `unregisterBlockEventHandlers(blockID)`
- Event dispatcher manages callback lifetime

## Future Enhancements

### Planned Features

1. **World Integration** - Currently actions log but don't modify the world
   - Requires passing World* reference to action execution
   - Should be added in a future PR

2. **Biome Event Handlers** - Extend system to support biome-level events
   - Parse "events" section in biome YAML files
   - Register handlers with biome ID filtering

3. **Conditional Actions** - Add if/else logic
   - Check block properties before executing
   - Support boolean expressions

4. **Variables** - Store and retrieve state
   - Per-block metadata storage
   - Global variable registry

5. **Timers** - Schedule delayed actions
   - Action queue with timing
   - Periodic action execution

6. **Entity Targeting** - Target specific entities
   - Filter by entity type
   - Apply effects to entities

7. **Custom Events** - User-defined event types
   - Script-to-script communication
   - Cross-block coordination

### Integration Points

To fully integrate this system, the following components need modification:

1. **World.cpp** - Add methods to execute block/structure placement
2. **ParticleSystem.cpp** - Add particle spawning API
3. **SoundSystem.cpp** - Add sound playback API
4. **CommandProcessor.cpp** - Add command execution API

## Example Use Cases

### 1. Interactive Mechanisms

```yaml
# Lever that powers nearby blocks
events:
  on_interact:
    - type: trigger_update
      offset: [1, 0, 0]
    - type: trigger_update
      offset: [-1, 0, 0]
    - type: play_sound
      sound: "click"
```

### 2. Environmental Effects

```yaml
# Fire block that spreads
events:
  on_update:
    - type: place_block
      block: "Fire"
      offset: [1, 0, 0]
      probability: 10
    - type: place_block
      block: "Fire"
      offset: [-1, 0, 0]
      probability: 10
```

### 3. Trap Blocks

```yaml
# Spike trap
events:
  on_step:
    - type: spawn_particles
      particle: "blood"
      offset: [0, 1, 0]
    - type: run_command
      command: "damage_player 5"
```

### 4. Resource Generators

```yaml
# Ore that regenerates
events:
  on_break:
    - type: trigger_update
      offset: [0, 0, 0]
  on_update:
    - type: place_block
      block: "Iron Ore"
      offset: [0, 0, 0]
      probability: 1  # 1% chance per update
```

## Testing

Example blocks have been created in `/home/user/voxel-engine/assets/blocks/`:

1. **pressure_plate.yaml** - Basic event handling example
2. **explosive_ore.yaml** - Multi-action event with probabilities
3. **grass_spreader.yaml** - Block placement and spreading behavior

To test:
1. Load the game with the example blocks
2. Place the blocks in the world
3. Trigger events (break, step on, etc.)
4. Check console output for action logging

## Code Quality

### Error Handling

- All YAML parsing wrapped in try-catch blocks
- Malformed YAML logs warnings but doesn't crash
- Missing required fields throw descriptive errors
- Invalid action types handled gracefully

### Performance

- Event handlers use efficient filtering (only relevant blocks respond)
- Probability checks are fast (single random number generation)
- Handler storage uses hash maps for O(1) lookup
- Minimal overhead when events section is missing

### Maintainability

- Clear separation of concerns (parsing, execution, registration)
- Extensive documentation and comments
- Consistent naming conventions
- Helper functions for common operations

## Files Modified

- `/home/user/voxel-engine/include/script_action.h` (new)
- `/home/user/voxel-engine/src/script_action.cpp` (new)
- `/home/user/voxel-engine/src/block_system.cpp` (modified)

## Files Created

- `/home/user/voxel-engine/assets/blocks/pressure_plate.yaml` (example)
- `/home/user/voxel-engine/assets/blocks/explosive_ore.yaml` (example)
- `/home/user/voxel-engine/assets/blocks/grass_spreader.yaml` (example)
- `/home/user/voxel-engine/docs/yaml_scripting_guide.md` (documentation)
- `/home/user/voxel-engine/docs/script_action_implementation.md` (this file)

## Conclusion

The YAML script action system provides a powerful, flexible way for content creators to add interactive behaviors to blocks without writing C++ code. The system integrates seamlessly with the existing event dispatcher and block registry, and is designed for easy extension and maintenance.
