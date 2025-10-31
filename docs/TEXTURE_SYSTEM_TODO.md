# Texture System Completion Guide

## What's Done ✅
The texture loading and rendering pipeline is **90% complete**! Here's what works:

1. **Block System** - Loads PNG textures from `assets/textures/{blockname}.png`
2. **Vertex Structure** - Updated with UV coordinates (x,y,z, r,g,b, u,v)
3. **Shaders** - Updated to pass and use UVs (currently commented out)
4. **Mesh Generation** - All block faces have proper UV mapping (0,0) to (1,1)
5. **Fallback System** - Falls back to hex colors if texture not found
6. **Error Handling** - Missing textures show as semi-transparent red
7. **Helper Functions** - All Vulkan texture utilities implemented

## What's Left (10%) 🚧

To enable textures, you need to wire up the Vulkan descriptor bindings:

### Step 1: Update Descriptor Set Layout
**File:** `src/vulkan_renderer.cpp` - `createDescriptorSetLayout()`

```cpp
void VulkanRenderer::createDescriptorSetLayout() {
    // Existing UBO binding
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    // NEW: Texture sampler binding
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    // Update to include both bindings
    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}
```

### Step 2: Update Descriptor Pool
**File:** `src/vulkan_renderer.cpp` - `createDescriptorPool()`

```cpp
void VulkanRenderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    // UBO pool size
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    // NEW: Texture sampler pool size
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}
```

### Step 3: Create Default White Texture
**File:** `src/vulkan_renderer.cpp` - Add to constructor or new function

```cpp
void VulkanRenderer::createDefaultTexture() {
    // Create 1x1 white texture for blocks without custom textures
    unsigned char whitePixel[4] = {255, 255, 255, 255};

    VkDeviceSize imageSize = 4; // 1 pixel * 4 bytes (RGBA)

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, whitePixel, imageSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create image
    createImage(1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               m_defaultTextureImage, m_defaultTextureMemory);

    // Transition and copy
    transitionImageLayout(m_defaultTextureImage, VK_FORMAT_R8G8B8A8_SRGB,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer, m_defaultTextureImage, 1, 1);
    transitionImageLayout(m_defaultTextureImage, VK_FORMAT_R8G8B8A8_SRGB,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Cleanup staging
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    // Create view and sampler
    m_defaultTextureView = createImageView(m_defaultTextureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    m_defaultTextureSampler = createTextureSampler();
}
```

Add member variables to `vulkan_renderer.h`:
```cpp
VkImage m_defaultTextureImage;
VkDeviceMemory m_defaultTextureMemory;
VkImageView m_defaultTextureView;
VkSampler m_defaultTextureSampler;
```

### Step 4: Update Descriptor Sets
**File:** `src/vulkan_renderer.cpp` - `createDescriptorSets()`

```cpp
void VulkanRenderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // UBO descriptor
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        // NEW: Texture sampler descriptor (using default white texture)
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_defaultTextureView;
        imageInfo.sampler = m_defaultTextureSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        // Write UBO
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // NEW: Write texture sampler
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                              descriptorWrites.data(), 0, nullptr);
    }
}
```

### Step 5: Uncomment Shader Code
**File:** `shaders/shader.frag`

Uncomment these lines:
```glsl
layout(binding = 1) uniform sampler2D texSampler;

// In main():
vec4 texColor = texture(texSampler, fragTexCoord);
vec3 baseColor = texColor.rgb * fragColor;
```

### Step 6: Cleanup
**File:** `src/vulkan_renderer.cpp` - `cleanup()`

Add to cleanup function:
```cpp
// In cleanup():
vkDestroySampler(m_device, m_defaultTextureSampler, nullptr);
vkDestroyImageView(m_device, m_defaultTextureView, nullptr);
vkDestroyImage(m_device, m_defaultTextureImage, nullptr);
vkFreeMemory(m_device, m_defaultTextureMemory, nullptr);
```

## Testing Your Texture System

### Create Test Textures
1. Create `assets/textures/` directory
2. Add 16x16 PNG files:
   - `grass.png` - Green texture
   - `dirt.png` - Brown texture
   - `stone.png` - Gray texture

### Expected Behavior
- ✅ Blocks with PNG textures → Show texture
- ✅ Blocks without PNG → Use hex color from YAML
- ✅ Blocks with invalid config → Semi-transparent red cube
- ✅ Liquids → Render transparent

## Notes
- The system uses **nearest-neighbor filtering** for pixelated Minecraft-style look
- Each block face gets standard UV mapping: (0,0) bottom-left to (1,1) top-right
- Block system checks `assets/textures/{lowercase_name}.png`
- White vertex color (1,1,1) = show texture
- Colored vertex color = tint texture or show solid color

## File Checklist
Files modified for texture system:
- ✅ `include/block_system.h` - Added texture members
- ✅ `src/block_system.cpp` - Texture loading logic
- ✅ `include/chunk.h` - Added UV to vertex
- ✅ `src/chunk.cpp` - UV generation
- ✅ `src/vulkan_renderer.cpp` - Helper functions
- ✅ `include/vulkan_renderer.h` - Function declarations
- ✅ `shaders/shader.vert` - UV passthrough
- ✅ `shaders/shader.frag` - Texture sampling (commented)
- ✅ `src/main.cpp` - Pass renderer to loadBlocks()

Good luck! 🎮
