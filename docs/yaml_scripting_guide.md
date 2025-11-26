# YAML Scripting Guide

## Overview

The YAML scripting system allows you to define event-driven block behaviors entirely in YAML without writing C++ code. This makes it easy for content creators to add interactive blocks with complex behaviors.

## Basic Concepts

### Events

Events are triggered when specific actions happen to a block. The following event types are supported:

- `on_break` - Fires when a block is broken
- `on_place` - Fires when a block is placed
- `on_step` - Fires when an entity steps on the block
- `on_neighbor_change` - Fires when an adjacent block changes
- `on_interact` - Fires when a player right-clicks the block
- `on_update` - Fires when the block receives a scheduled update tick

### Actions

Actions are operations that execute when an event fires. Each action has a type and parameters.

#### Action Types

1. **place_block** - Places a block at a position
   - `block`: Name of the block to place
   - `offset`: Relative position [x, y, z]
   - `probability`: Chance to execute (0-100)

2. **break_block** - Breaks a block at a position
   - `offset`: Relative position [x, y, z]
   - `probability`: Chance to execute (0-100)

3. **spawn_structure** - Spawns a structure at a position
   - `structure`: Name of the structure to spawn
   - `offset`: Relative position [x, y, z]
   - `probability`: Chance to execute (0-100)

4. **spawn_particles** - Spawns particle effects
   - `particle`: Name of the particle effect
   - `offset`: Relative position [x, y, z]
   - `probability`: Chance to execute (0-100)

5. **play_sound** - Plays a sound effect (future feature)
   - `sound`: Name of the sound to play
   - `probability`: Chance to execute (0-100)

6. **run_command** - Executes a console command
   - `command`: Command string to execute
   - `probability`: Chance to execute (0-100)

7. **set_metadata** - Sets block metadata
   - `metadata`: Key-value pairs to set
   - `offset`: Relative position [x, y, z]
   - `probability`: Chance to execute (0-100)

8. **trigger_update** - Schedules a block update tick
   - `offset`: Relative position [x, y, z]
   - `probability`: Chance to execute (0-100)

## Examples

### Example 1: Pressure Plate

A pressure plate that triggers events when stepped on:

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

  on_neighbor_change:
    - type: break_block
      offset: [0, 0, 0]
      probability: 50  # 50% chance to pop off
```

### Example 2: Explosive Ore

An ore that explodes when broken:

```yaml
id: 51
name: "Explosive Ore"
texture: "explosive_ore.png"
durability: 5
affected_by_gravity: false

events:
  on_break:
    # Spawn explosion particles
    - type: spawn_particles
      particle: "explosion"
      offset: [0, 0, 0]

    # Break surrounding blocks
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

    # Log the explosion
    - type: run_command
      command: "echo Explosive ore detonated!"
```

### Example 3: Grass Spreader

A grass block that spreads to nearby dirt:

```yaml
id: 52
name: "Grass Spreader"
texture: "grass_top.png"
durability: 3
affected_by_gravity: false

events:
  on_update:
    # Spread to neighboring blocks
    - type: place_block
      block: "Grass Block"
      offset: [1, 0, 0]
      probability: 25

    - type: place_block
      block: "Grass Block"
      offset: [-1, 0, 0]
      probability: 25

    # Spawn flowers occasionally
    - type: place_block
      block: "Flower"
      offset: [0, 1, 0]
      probability: 5

  on_step:
    - type: spawn_particles
      particle: "grass_particles"
      offset: [0, 1, 0]
      probability: 10
```

### Example 4: Hidden Treasure

A block that spawns a structure when broken:

```yaml
id: 53
name: "Ancient Stone"
texture: "ancient_stone.png"
durability: 10
affected_by_gravity: false

events:
  on_break:
    - type: spawn_structure
      structure: "hidden_treasure"
      offset: [0, -1, 0]
      probability: 50  # 50% chance to reveal treasure

    - type: run_command
      command: "echo You discovered a hidden treasure!"
      probability: 50
```

## Advanced Usage

### Multiple Actions Per Event

You can define multiple actions for a single event. They will execute in order:

```yaml
events:
  on_break:
    - type: spawn_particles
      particle: "smoke"
      offset: [0, 0, 0]

    - type: play_sound
      sound: "glass_break"

    - type: run_command
      command: "echo Block broken!"
```

### Probability-Based Actions

Use the `probability` field to create random behaviors:

```yaml
events:
  on_place:
    # 80% chance to place normally
    - type: run_command
      command: "echo Normal placement"
      probability: 80

    # 20% chance to spawn TNT instead!
    - type: place_block
      block: "TNT"
      offset: [0, 0, 0]
      probability: 20
```

### Offset Positioning

The `offset` field is relative to the event position:

```yaml
events:
  on_break:
    # Place blocks in a cross pattern
    - type: place_block
      block: "Stone"
      offset: [1, 0, 0]   # East

    - type: place_block
      block: "Stone"
      offset: [-1, 0, 0]  # West

    - type: place_block
      block: "Stone"
      offset: [0, 0, 1]   # South

    - type: place_block
      block: "Stone"
      offset: [0, 0, -1]  # North
```

## Best Practices

1. **Use descriptive block names** - Makes scripts easier to understand
2. **Test probability values** - Balance randomness carefully
3. **Consider performance** - Avoid too many actions on frequent events like `on_step`
4. **Chain events carefully** - Avoid infinite loops (e.g., placing a block that triggers itself)
5. **Document complex behaviors** - Add comments to explain your script logic

## Troubleshooting

### Actions Not Executing

- Check that the event type is spelled correctly (e.g., `on_break` not `onBreak`)
- Verify the action type is valid (e.g., `place_block` not `placeBlock`)
- Ensure required fields are present (e.g., `block` for `place_block`)

### Blocks Not Loading

- Check YAML syntax (proper indentation, colons, hyphens)
- Verify block ID is unique
- Ensure all required fields are present (`name`, `durability`, etc.)

### Probability Not Working

- Probability is a percentage (0-100), not a decimal (0.0-1.0)
- Value of 100 = always execute, 0 = never execute

## Future Enhancements

Planned features for future versions:

- Conditional actions (if/else logic)
- Variable storage and retrieval
- Timer-based actions
- Entity targeting and manipulation
- Custom event types
- Biome-specific event handlers
