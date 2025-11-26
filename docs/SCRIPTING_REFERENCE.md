# Voxel Engine Scripting Reference

Complete guide to the YAML-based scripting system for blocks and biomes.

---

## Quick Start

Add an `events` section to any block or biome YAML file:

```yaml
name: "My Block"
texture: "my_block.png"
durability: 3

events:
  on_break:
    - type: run_command
      command: "echo Block broken!"
    - type: spawn_particles
      particle: "explosion"
```

---

## Block Events

| Event | Trigger | Cancellable |
|-------|---------|-------------|
| `on_break` | Block is broken | Yes |
| `on_place` | Block is placed | Yes |
| `on_step` | Entity steps on block | No |
| `on_interact` | Player right-clicks | Yes |
| `on_neighbor_change` | Adjacent block changes | No |
| `on_update` | Scheduled tick fires | No |

## Biome Events

| Event | Trigger |
|-------|---------|
| `on_enter` | Player enters biome |
| `on_exit` | Player leaves biome |
| `on_chunk_generate` | Chunk generated in biome |

---

## Actions

### Block Manipulation

**place_block** - Place a block
```yaml
- type: place_block
  block: "Stone"
  offset: [1, 0, 0]
  probability: 75
```

**break_block** - Break a block
```yaml
- type: break_block
  offset: [0, -1, 0]
  probability: 100
```

### Structures & Effects

**spawn_structure** - Spawn a structure
```yaml
- type: spawn_structure
  structure: "Oak Tree"
  offset: [0, 1, 0]
```

**spawn_particles** - Spawn particles
```yaml
- type: spawn_particles
  particle: "explosion"
  offset: [0, 0, 0]
  intensity: 1.0
```

### Commands & Metadata

**run_command** - Execute console command
```yaml
- type: run_command
  command: "say Hello!"
```

**set_metadata** - Set block metadata
```yaml
- type: set_metadata
  metadata:
    state: "active"
    power: "5"
```

**trigger_update** - Schedule block update
```yaml
- type: trigger_update
  offset: [0, 0, 0]
```

---

## Conditionals

Execute actions based on conditions:

```yaml
- type: conditional
  condition: "random_chance"
  condition_value: "50"
  then:
    - type: place_block
      block: "Gold"
  else:
    - type: run_command
      command: "echo No luck!"
```

### Condition Types

| Condition | `condition_value` | Description |
|-----------|-------------------|-------------|
| `random_chance` | `"0-100"` | Random probability (integer %) |
| `block_is` | `"Block Name"` | True if block at offset matches |
| `block_is_not` | `"Block Name"` | True if block doesn't match |
| `time_is_day` | - | True during daytime (0-12000) |
| `time_is_night` | - | True during nighttime (12000-24000) |

---

## Variables

Store and retrieve state across events:

**set_variable** - Store a value
```yaml
- type: set_variable
  variable: "counter"
  value: "0"
```

**increment_var** / **decrement_var** - Modify numeric variable
```yaml
- type: increment_var
  variable: "counter"
  amount: 1
```

### Example: Counter-Based Trigger
```yaml
events:
  on_step:
    - type: increment_var
      variable: "steps"
      amount: 1
    - type: conditional
      condition: "variable_equals"
      variable: "steps"
      value: "10"
      then:
        - type: run_command
          command: "echo 10 steps reached!"
        - type: set_variable
          variable: "steps"
          value: "0"
```

---

## Common Parameters

All actions support:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `offset` | [x, y, z] | [0,0,0] | Relative position |
| `probability` | 0-100 | 100 | Chance to execute (%) |

---

## Examples

### Explosive Ore
```yaml
name: "Explosive Ore"
events:
  on_break:
    - type: spawn_particles
      particle: "explosion"
    - type: break_block
      offset: [1, 0, 0]
      probability: 75
    - type: break_block
      offset: [-1, 0, 0]
      probability: 75
    - type: break_block
      offset: [0, 1, 0]
      probability: 75
    - type: break_block
      offset: [0, -1, 0]
      probability: 75
```

### Pressure Plate
```yaml
name: "Pressure Plate"
events:
  on_step:
    - type: run_command
      command: "echo Activated!"
    - type: spawn_particles
      particle: "redstone"
      offset: [0, 1, 0]
  on_neighbor_change:
    - type: break_block
      offset: [0, 0, 0]
      probability: 50
```

### Lucky Block
```yaml
name: "Lucky Block"
events:
  on_break:
    - type: conditional
      condition: "random_chance"
      condition_value: "25"
      then:
        - type: spawn_structure
          structure: "Treasure Chest"
      else:
        - type: conditional
          condition: "random_chance"
          condition_value: "33"
          then:
            - type: place_block
              block: "Gold Block"
          else:
            - type: run_command
              command: "echo Better luck next time!"
```

### Biome Welcome Message
```yaml
name: "Magical Forest"
events:
  on_enter:
    - type: run_command
      command: "echo Welcome to the Magical Forest!"
    - type: spawn_particles
      particle: "sparkle"
  on_chunk_generate:
    - type: conditional
      condition: "random_chance"
      condition_value: "10"
      then:
        - type: spawn_structure
          structure: "Fairy Ring"
          offset: [8, 0, 8]
```

---

## Best Practices

1. **Keep it simple** - Complex logic belongs in C++
2. **Use probability** - Add randomness for natural effects
3. **Avoid loops** - Don't place blocks that trigger themselves
4. **Test thoroughly** - Event chains can have unexpected results
5. **Performance** - Limit actions on frequent events like `on_step`

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Actions not executing | Check event/action spelling (use snake_case) |
| Block not loading | Verify YAML syntax and required fields |
| Probability not working | Use 0-100 (percent), not 0.0-1.0 |
| Conditionals failing | Check condition type and parameter names |
| Variables not persisting | Variables are global, check spelling |

---

## Event System (C++ API)

For C++ developers integrating with the event system:

### Initialize
```cpp
auto& dispatcher = EventDispatcher::instance();
dispatcher.start();  // In main.cpp after init
dispatcher.stop();   // In shutdown
```

### Subscribe
```cpp
auto handle = dispatcher.subscribe(
    EventType::BLOCK_BREAK,
    [](Event& e) {
        auto& evt = static_cast<BlockBreakEvent&>(e);
        // Handle event
    },
    EventPriority::NORMAL,
    "my_system"
);
```

### Dispatch
```cpp
dispatcher.dispatch(std::make_unique<BlockBreakEvent>(
    position, blockID, BreakCause::PLAYER, entityID
));
```

### Priorities
- **HIGHEST** - Security, protection
- **HIGH** - Core game logic
- **NORMAL** - Default
- **LOW** - Cosmetic effects
- **LOWEST** - Fallback handlers
- **MONITOR** - Logging only (always runs)

### Cancel Events
```cpp
dispatcher.subscribe(EventType::BLOCK_BREAK, [](Event& e) {
    if (isProtected(position)) {
        e.cancel();
    }
}, EventPriority::HIGHEST, "protection");
```

---

See also: [ENGINE_HANDBOOK.md Section 6.6](ENGINE_HANDBOOK.md#66-yaml-scripting-system)
