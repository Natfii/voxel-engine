# Voxel Engine Refactoring Guide

This guide documents the refactoring infrastructure added to improve code quality
and reduce duplication in the voxel engine codebase.

## Overview

The refactoring introduces two key components:

1. **PipelineBuilder** - Fluent builder for Vulkan graphics pipelines
2. **FaceConfig** - Data-driven configuration for chunk mesh generation

These can be incrementally adopted to replace duplicated code.

## 1. PipelineBuilder Usage

### Location
- Header: `include/vulkan/pipeline_builder.h`
- Implementation: `src/vulkan/pipeline_builder.cpp`

### Current Problem
`vulkan_renderer.cpp` contains 7 nearly-identical pipeline creation methods
(~130 lines each), totaling ~900 lines of duplicated code:
- `createGraphicsPipeline()`
- `createTransparentPipeline()` - Only differs in `depthWriteEnable`
- `createWireframePipeline()` - Only differs in `polygonMode`
- etc.

### Solution
Replace each method with a PipelineBuilder call:

```cpp
// Before: 130 lines per pipeline
void VulkanRenderer::createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/vert.spv");
    // ... 125 more lines of boilerplate ...
}

// After: 8 lines per pipeline
void VulkanRenderer::createAllPipelines() {
    auto binding = CompressedVertex::getBindingDescription();
    auto attrs = CompressedVertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrVec(attrs.begin(), attrs.end());

    // Graphics pipeline (opaque)
    m_graphicsPipeline = PipelineBuilder(m_device, m_renderPass)
        .setShaders("shaders/vert.spv", "shaders/frag.spv")
        .setVertexInput(binding, attrVec)
        .setViewport(m_swapChainExtent.width, m_swapChainExtent.height)
        .setDescriptorSetLayout(m_descriptorSetLayout)
        .build(&m_pipelineLayout);

    // Transparent pipeline (same but no depth write)
    m_transparentPipeline = PipelineBuilder(m_device, m_renderPass)
        .setShaders("shaders/vert.spv", "shaders/frag.spv")
        .setVertexInput(binding, attrVec)
        .setViewport(m_swapChainExtent.width, m_swapChainExtent.height)
        .setDepthWrite(false)  // Only difference!
        .setDescriptorSetLayout(m_descriptorSetLayout)
        .setPipelineLayout(m_pipelineLayout)  // Reuse layout
        .build();

    // Wireframe pipeline
    m_wireframePipeline = PipelineBuilder(m_device, m_renderPass)
        .setShaders("shaders/vert.spv", "shaders/frag.spv")
        .setVertexInput(binding, attrVec)
        .setViewport(m_swapChainExtent.width, m_swapChainExtent.height)
        .setPolygonMode(VK_POLYGON_MODE_LINE)  // Only difference!
        .setDescriptorSetLayout(m_descriptorSetLayout)
        .setPipelineLayout(m_pipelineLayout)
        .build();
}
```

### Migration Steps

1. Add include to `vulkan_renderer.cpp`:
   ```cpp
   #include "vulkan/pipeline_builder.h"
   ```

2. Create a new method `createAllPipelines()` that uses PipelineBuilder

3. Comment out old methods one at a time, testing after each

4. Once all working, remove the old methods

### Benefits
- ~900 lines reduced to ~100 lines
- Easier to add new pipelines
- Consistent configuration across pipelines
- Self-documenting code

## 2. FaceConfig for Chunk Mesh Generation

### Location
- Header: `include/chunk_face_config.h`

### Current Problem
`chunk.cpp` contains 6 nearly-identical blocks for processing each face direction
(~60 lines each), totaling ~360 lines of duplicated code. Each block:
- Queries neighbor block
- Checks if face should render
- Extends with greedy meshing
- Marks blocks as processed
- Generates vertices

### Solution
Use data-driven configuration with `FaceConfig`:

```cpp
// Before: 60 lines repeated 6 times for each face
// Front face (z=0, facing -Z direction)
{
    if (!IS_PROCESSED_NEGZ(X, Y, Z)) {
        int neighborBlockID = getBlockID(X, Y, Z - 1);
        // ... 55 more lines ...
    }
}
// Back face (z=0.5, facing +Z direction)
{
    if (!IS_PROCESSED_POSZ(X, Y, Z)) {
        // ... same 60 lines with different constants ...
    }
}
// ... 4 more faces ...

// After: Single loop using FaceConfig
#include "chunk_face_config.h"

for (const auto& face : FACE_CONFIGS) {
    processFace(x, y, z, face, blockId, def);
}
```

### Helper Functions in FaceConfig

The header provides:

1. **`shouldRenderFace()`** - Determines if a face should render based on:
   - Solid blocks: render if neighbor is not solid
   - Liquid blocks: render only if neighbor is air
   - Transparent blocks: render if neighbor is different type

2. **`calculateGreedyExtents()`** - Template function for greedy meshing:
   - Extends face in two perpendicular directions
   - Returns (width, height) of merged quad

### Migration Steps

1. Add include to `chunk.cpp`:
   ```cpp
   #include "chunk_face_config.h"
   ```

2. Create helper method `processFace()` that takes `FaceConfig`

3. Replace the 6 face blocks with a loop over `FACE_CONFIGS`

4. Test thoroughly - mesh generation is performance-critical

### Benefits
- ~360 lines reduced to ~100 lines
- Easier to modify face processing logic
- Consistent behavior across all faces
- Configuration is visible and documented

## 3. Future Refactoring: VulkanRenderer Decomposition

The `VulkanRenderer` class (4,184 lines) violates Single Responsibility Principle.
Suggested decomposition:

```
include/vulkan/
├── vulkan_context.h       # Instance, device, queues
├── swapchain_manager.h    # Swapchain creation/recreation
├── pipeline_builder.h     # Already implemented!
├── buffer_manager.h       # Buffer creation utilities
├── texture_manager.h      # Texture loading/atlas
├── skybox_renderer.h      # Skybox-specific rendering
└── descriptor_manager.h   # Descriptor sets/layouts

src/vulkan/
├── vulkan_context.cpp
├── swapchain_manager.cpp
├── pipeline_builder.cpp   # Already implemented!
├── buffer_manager.cpp
├── texture_manager.cpp
├── skybox_renderer.cpp
└── descriptor_manager.cpp
```

This is a larger refactoring that should be done incrementally:
1. Extract one component at a time
2. Test after each extraction
3. Update includes gradually

## Testing Checklist

After each refactoring step:

- [ ] Project builds without errors
- [ ] No new warnings introduced
- [ ] Game runs without crashes
- [ ] Visual output is identical
- [ ] Performance is not degraded (use F1 debug overlay)
- [ ] All chunk types render correctly (opaque, transparent, water)
- [ ] Skybox renders correctly
- [ ] All pipelines work (wireframe mode, line rendering)

## Code Quality Metrics

Before refactoring:
- `vulkan_renderer.cpp`: 4,184 lines
- `chunk.cpp`: 2,958 lines
- Estimated duplicated code: ~1,500 lines

Target after full refactoring:
- No file > 500 lines
- No method > 50 lines
- < 100 lines duplicated code
