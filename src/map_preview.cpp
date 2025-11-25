/**
 * @file map_preview.cpp
 * @brief Live map preview implementation
 *
 * Created: 2025-11-25
 */

#include "map_preview.h"
#include "biome_map.h"
#include "vulkan_renderer.h"
#include "terrain_constants.h"
#include "logger.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <cstring>

using namespace TerrainGeneration;

MapPreview::MapPreview() {
    m_pixels.resize(MAP_SIZE * MAP_SIZE, 0xFF000000);  // Black with alpha
    m_lastRevealTime = std::chrono::steady_clock::now();
}

MapPreview::~MapPreview() {
    cleanup();
}

bool MapPreview::initialize(BiomeMap* biomeMap, VulkanRenderer* renderer, int centerX, int centerZ) {
    if (!biomeMap || !renderer) {
        Logger::error() << "MapPreview::initialize: null biomeMap or renderer";
        return false;
    }

    m_biomeMap = biomeMap;
    m_renderer = renderer;
    m_centerX = centerX;
    m_centerZ = centerZ;

    VkDevice device = renderer->getDevice();
    VkPhysicalDevice physicalDevice = renderer->getPhysicalDevice();

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = MAP_SIZE;
    imageInfo.extent.height = MAP_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        Logger::error() << "MapPreview: Failed to create image";
        return false;
    }

    // Allocate image memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_image, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_imageMemory) != VK_SUCCESS) {
        Logger::error() << "MapPreview: Failed to allocate image memory";
        return false;
    }

    vkBindImageMemory(device, m_image, m_imageMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        Logger::error() << "MapPreview: Failed to create image view";
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        Logger::error() << "MapPreview: Failed to create sampler";
        return false;
    }

    // Create staging buffer for CPU->GPU transfer
    VkDeviceSize bufferSize = MAP_SIZE * MAP_SIZE * 4;  // RGBA

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS) {
        Logger::error() << "MapPreview: Failed to create staging buffer";
        return false;
    }

    VkMemoryRequirements stagingMemReq;
    vkGetBufferMemoryRequirements(device, m_stagingBuffer, &stagingMemReq);

    uint32_t stagingMemType = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((stagingMemReq.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            stagingMemType = i;
            break;
        }
    }

    VkMemoryAllocateInfo stagingAllocInfo{};
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = stagingMemReq.size;
    stagingAllocInfo.memoryTypeIndex = stagingMemType;

    if (vkAllocateMemory(device, &stagingAllocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) {
        Logger::error() << "MapPreview: Failed to allocate staging memory";
        return false;
    }

    vkBindBufferMemory(device, m_stagingBuffer, m_stagingMemory, 0);

    // Create ImGui descriptor for rendering
    m_imguiDescriptor = ImGui_ImplVulkan_AddTexture(m_sampler, m_imageView,
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_initialized = true;
    Logger::info() << "MapPreview initialized (" << MAP_SIZE << "x" << MAP_SIZE << ")";
    return true;
}

void MapPreview::generateFullPreview() {
    if (!m_initialized || !m_biomeMap) return;

    std::lock_guard<std::mutex> lock(m_pixelMutex);

    // Calculate world bounds covered by the preview
    int halfSize = (MAP_SIZE * BLOCKS_PER_PIXEL) / 2;

    for (int py = 0; py < MAP_SIZE; py++) {
        for (int px = 0; px < MAP_SIZE; px++) {
            // Convert pixel to world coordinates
            float worldX = static_cast<float>(m_centerX + (px - MAP_SIZE/2) * BLOCKS_PER_PIXEL);
            float worldZ = static_cast<float>(m_centerZ + (py - MAP_SIZE/2) * BLOCKS_PER_PIXEL);

            m_pixels[py * MAP_SIZE + px] = sampleTerrainColor(worldX, worldZ);
        }
    }

    m_needsUpdate = true;
}

void MapPreview::markChunkGenerated(int chunkX, int chunkZ) {
    if (!m_initialized) return;

    // Queue the chunk for animated reveal instead of immediate update
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingChunks.push({chunkX, chunkZ, std::chrono::steady_clock::now()});
}

// Helper to actually reveal a chunk (called from animation loop)
void MapPreview::revealChunk(int chunkX, int chunkZ) {
    // Convert chunk coordinates to pixel coordinates
    int worldX = chunkX * 32;  // Chunk size is 32
    int worldZ = chunkZ * 32;

    // Calculate pixel range for this chunk
    int startPx = (worldX - m_centerX) / BLOCKS_PER_PIXEL + MAP_SIZE/2;
    int startPy = (worldZ - m_centerZ) / BLOCKS_PER_PIXEL + MAP_SIZE/2;
    int endPx = startPx + 32 / BLOCKS_PER_PIXEL;
    int endPy = startPy + 32 / BLOCKS_PER_PIXEL;

    // Clamp to valid range
    startPx = std::max(0, startPx);
    startPy = std::max(0, startPy);
    endPx = std::min(MAP_SIZE, endPx);
    endPy = std::min(MAP_SIZE, endPy);

    if (startPx >= MAP_SIZE || startPy >= MAP_SIZE || endPx <= 0 || endPy <= 0) {
        return;  // Chunk outside preview area
    }

    std::lock_guard<std::mutex> lock(m_pixelMutex);

    // Brighten the chunk area to show it's generated
    for (int py = startPy; py < endPy; py++) {
        for (int px = startPx; px < endPx; px++) {
            uint32_t& pixel = m_pixels[py * MAP_SIZE + px];
            // Add a slight brightness boost to show generation
            uint8_t r = (pixel >> 0) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;

            // Boost brightness by ~50% for more visible effect
            r = std::min(255, static_cast<int>(r * 1.5f));
            g = std::min(255, static_cast<int>(g * 1.5f));
            b = std::min(255, static_cast<int>(b * 1.5f));

            pixel = 0xFF000000 | (b << 16) | (g << 8) | r;
        }
    }

    m_needsUpdate = true;
}

void MapPreview::updateTexture() {
    if (!m_initialized) return;

    // ============================================================================
    // ANIMATED CHUNK REVEAL (2025-11-25)
    // Process pending chunks with staggered timing for smooth animation
    // ============================================================================
    auto now = std::chrono::steady_clock::now();

    // Collect chunks to reveal (outside lock to avoid deadlock)
    std::vector<std::pair<int, int>> toReveal;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);

        if (!m_pendingChunks.empty()) {
            // Calculate how many chunks to reveal based on elapsed time
            auto elapsed = std::chrono::duration<float>(now - m_lastRevealTime).count();
            int chunksToProcess = static_cast<int>(elapsed * m_chunksPerSecond);
            chunksToProcess = std::min(chunksToProcess, static_cast<int>(m_pendingChunks.size()));

            // Always process at least 1 chunk if we have pending and enough time passed
            if (chunksToProcess > 0 || (elapsed > 0.02f && !m_pendingChunks.empty())) {
                chunksToProcess = std::max(1, chunksToProcess);

                for (int i = 0; i < chunksToProcess && !m_pendingChunks.empty(); i++) {
                    auto chunk = m_pendingChunks.front();
                    m_pendingChunks.pop();
                    toReveal.push_back({chunk.chunkX, chunk.chunkZ});
                }

                m_lastRevealTime = now;
            }
        }
    }

    // Reveal chunks (outside pending mutex)
    for (const auto& [cx, cz] : toReveal) {
        revealChunk(cx, cz);
    }

    if (!m_needsUpdate) return;

    VkDevice device = m_renderer->getDevice();

    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(device, m_stagingMemory, 0, MAP_SIZE * MAP_SIZE * 4, 0, &data);
    {
        std::lock_guard<std::mutex> lock(m_pixelMutex);
        memcpy(data, m_pixels.data(), MAP_SIZE * MAP_SIZE * 4);
    }
    vkUnmapMemory(device, m_stagingMemory);

    // Transfer to GPU image
    VkCommandBuffer cmd = m_renderer->beginSingleTimeCommands();

    // Transition image to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {MAP_SIZE, MAP_SIZE, 1};

    vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_renderer->endSingleTimeCommands(cmd);

    m_needsUpdate = false;
}

uint32_t MapPreview::sampleTerrainColor(float worldX, float worldZ) {
    if (!m_biomeMap) return 0xFF000000;

    // Get terrain height and biome info
    float height = m_biomeMap->getTerrainHeightAt(worldX, worldZ);
    int temperature = m_biomeMap->getTemperatureAt(worldX, worldZ);
    int moisture = m_biomeMap->getMoistureAt(worldX, worldZ);

    return biomeToColor(temperature, moisture, height);
}

uint32_t MapPreview::biomeToColor(int temperature, int moisture, float height) {
    uint8_t r, g, b;

    // Water (below sea level)
    if (height < WATER_LEVEL - 2) {
        // Deep water
        float depth = (WATER_LEVEL - height) / 20.0f;
        depth = std::min(1.0f, depth);
        r = static_cast<uint8_t>(30 - depth * 20);
        g = static_cast<uint8_t>(80 - depth * 30);
        b = static_cast<uint8_t>(150 - depth * 50);
        return 0xFF000000 | (b << 16) | (g << 8) | r;
    }

    // Beach (near water level)
    if (height < WATER_LEVEL + 2) {
        r = 194; g = 178; b = 128;  // Sand color
        return 0xFF000000 | (b << 16) | (g << 8) | r;
    }

    // Biome-based coloring
    // Cold biomes (temperature < 30)
    if (temperature < 30) {
        if (moisture > 60) {
            // Snowy taiga
            r = 200; g = 220; b = 230;  // Light blue-white
        } else {
            // Tundra/snow
            r = 240; g = 245; b = 250;  // Snow white
        }
    }
    // Hot biomes (temperature > 70)
    else if (temperature > 70) {
        if (moisture < 30) {
            // Desert
            r = 210; g = 180; b = 110;  // Sandy yellow
        } else if (moisture > 60) {
            // Jungle
            r = 30; g = 130; b = 30;  // Dark green
        } else {
            // Savanna
            r = 170; g = 160; b = 80;  // Yellow-green
        }
    }
    // Temperate biomes
    else {
        if (moisture > 70) {
            // Swamp
            r = 70; g = 100; b = 60;  // Dark murky green
        } else if (moisture > 40) {
            // Forest
            r = 40; g = 120; b = 40;  // Green
        } else {
            // Plains
            r = 100; g = 160; b = 60;  // Light green
        }
    }

    // Altitude shading (mountains get lighter/rockier)
    if (height > BASE_HEIGHT + 20) {
        float rockFactor = (height - BASE_HEIGHT - 20) / 30.0f;
        rockFactor = std::min(1.0f, rockFactor);

        // Blend toward gray (rock)
        r = static_cast<uint8_t>(r + (128 - r) * rockFactor);
        g = static_cast<uint8_t>(g + (128 - g) * rockFactor);
        b = static_cast<uint8_t>(b + (128 - b) * rockFactor);
    }

    // Darken base color slightly (will be brightened when chunk generates)
    r = static_cast<uint8_t>(r * 0.6f);
    g = static_cast<uint8_t>(g * 0.6f);
    b = static_cast<uint8_t>(b * 0.6f);

    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

void MapPreview::cleanup() {
    if (!m_initialized) return;

    VkDevice device = m_renderer->getDevice();

    // Wait for GPU to finish
    vkDeviceWaitIdle(device);

    if (m_imguiDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_imguiDescriptor);
        m_imguiDescriptor = VK_NULL_HANDLE;
    }

    if (m_stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_imageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_imageMemory, nullptr);
        m_imageMemory = VK_NULL_HANDLE;
    }

    m_initialized = false;
}
