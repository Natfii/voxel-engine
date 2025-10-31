# Targeting System

The targeting system is a unified system that handles block targeting, crosshair rendering, block outline rendering, and target information tracking.

## Overview

The targeting system replaces the old separate `crosshair` and `block_outline` systems with a single, efficient unified system that:
- Performs **one raycast per frame** (performance optimization)
- Provides rich block information
- Handles context-aware rendering (hides during menus/console)
- Integrates with the input manager for proper control flow

## Architecture

### Core Components

1. **TargetingSystem** (`targeting_system.h/cpp`)
   - Main system that orchestrates all targeting functionality
   - Single `update()` call per frame performs raycast
   - Renders crosshair (ImGui) and block outline (Vulkan)

2. **TargetInfo** (`target_info.h`)
   - Data structure containing all target information:
     - Block position and coordinates
     - Hit normal for placement
     - Distance to target
     - Block metadata (ID, name, type)
     - Breakability status
   - Helper methods for placement position calculations

3. **InputManager** (`input_manager.h/cpp`)
   - Context-aware input management
   - Four contexts: GAMEPLAY, MENU, CONSOLE, PAUSED
   - Controls when targeting/controls are enabled

## Usage

### Initialization

```cpp
// In main.cpp
TargetingSystem targetingSystem;
targetingSystem.init(&renderer);

// In cleanup
targetingSystem.cleanup(&renderer);
```

### Per-Frame Update

```cpp
// Single update call per frame
targetingSystem.setEnabled(InputManager::instance().isGameplayEnabled());
targetingSystem.update(&world, player.Position, player.Front);

// Get target info
const TargetInfo& target = targetingSystem.getTarget();

// Update outline buffer if target changed
if (target.hasTarget) {
    targetingSystem.updateOutlineBuffer(&renderer);
}
```

### Using Target Information

```cpp
const TargetInfo& target = targetingSystem.getTarget();

if (target.isValid() && target.isBreakable) {
    // Break block at target position
    world.breakBlock(target.blockPosition, &renderer);
}

// Check target details
if (target.hasTarget) {
    std::cout << "Block: " << target.blockName << std::endl;
    std::cout << "Type: " << target.blockType << std::endl;
    std::cout << "Distance: " << target.distance << "m" << std::endl;
}
```

### Rendering

```cpp
// Render crosshair (ImGui)
if (!console.isVisible() && !isPaused) {
    targetingSystem.renderCrosshair();
}

// Render block outline (Vulkan)
if (target.hasTarget) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.getLinePipeline());
    targetingSystem.renderBlockOutline(commandBuffer);
}
```

## Settings

The targeting system has several configurable settings:

```cpp
targetingSystem.setMaxDistance(5.0f);      // Max raycast distance
targetingSystem.setEnabled(true);          // Enable/disable system
targetingSystem.setCrosshairVisible(true); // Show/hide crosshair
targetingSystem.setOutlineVisible(true);   // Show/hide block outline
```

## Console Commands

- `debug targetinfo` - Toggle target information overlay

When enabled, shows in top-right corner:
- Block name and type
- Block coordinates
- Distance to target
- Whether block is breakable

## Performance

The targeting system is highly optimized:
- **Single raycast per frame** (previously 3+)
- Cached target info updated once per frame
- Block outline buffer only updated when target changes
- No redundant calculations

## Integration with Other Systems

### Input Manager
The targeting system respects input contexts:
```cpp
// Automatically disabled when console/menu is open
targetingSystem.setEnabled(InputManager::instance().isGameplayEnabled());

// Block breaking respects input context
if (InputManager::instance().canBreakBlocks()) {
    // Break block
}
```

### World System
Block breaking is encapsulated in World:
```cpp
// Simple interface
world.breakBlock(target.blockPosition, &renderer);
world.breakBlock(target.blockCoords, &renderer);

// Automatically handles:
// - Setting block to air
// - Regenerating chunk mesh
// - Updating neighbor chunks
```

### Block Registry
Target info is populated from Block Registry:
```cpp
target.blockName = BlockRegistry::instance().getBlockName(blockID);
target.blockType = BlockRegistry::instance().getBlockType(blockID);
target.isBreakable = BlockRegistry::instance().isBreakable(blockID);
```

## File Structure

```
include/
├── targeting_system.h   # Main targeting system
├── target_info.h        # Target data structure
└── input_manager.h      # Input context management

src/
├── targeting_system.cpp
└── input_manager.cpp
```

## Future Extensions

The targeting system is designed to be extensible for future features:

1. **Entity Targeting**
   - Add entity type to TargetInfo
   - Extend raycast to check entities
   - Add entity interaction handlers

2. **NPC Targeting**
   - Similar to entity targeting
   - Add dialogue/interaction system integration

3. **Advanced Placement**
   - Use hit normal for smart block placement
   - Rotation/orientation support

4. **Multi-block Targeting**
   - Select multiple blocks
   - Area operations (fill, replace)

## Technical Details

### Raycast Algorithm
Uses **DDA (Digital Differential Analyzer)** voxel traversal:
- Efficient grid traversal
- Exact block boundary detection
- Hit normal calculation for placement

### Rendering
- **Crosshair**: ImGui overlay (lightweight)
- **Block Outline**: Vulkan line rendering (12 lines forming cube wireframe)
- **Target Info**: ImGui window (debug overlay)

### Memory Management
- Outline vertex buffer managed by Vulkan renderer
- Target info is a simple struct (no heap allocation)
- Minimal per-frame overhead
