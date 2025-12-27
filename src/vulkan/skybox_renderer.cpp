/**
 * @file skybox_renderer.cpp
 * @brief Implementation of SkyboxRenderer class
 *
 * Extracted from VulkanRenderer to encapsulate skybox-specific rendering.
 */

#include "vulkan/skybox_renderer.h"
#include "vulkan/pipeline_builder.h"
#include <glm/glm.hpp>
#include <stdexcept>
#include <iostream>
#include <cstring>

// ============================================================================
// Construction / Destruction
// ============================================================================

SkyboxRenderer::SkyboxRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkCommandPool commandPool, VkQueue graphicsQueue)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_commandPool(commandPool)
    , m_graphicsQueue(graphicsQueue) {
}

SkyboxRenderer::~SkyboxRenderer() {
    cleanup();
}

SkyboxRenderer::SkyboxRenderer(SkyboxRenderer&& other) noexcept
    : m_device(other.m_device)
    , m_physicalDevice(other.m_physicalDevice)
    , m_commandPool(other.m_commandPool)
    , m_graphicsQueue(other.m_graphicsQueue)
    , m_pipeline(other.m_pipeline)
    , m_pipelineLayout(other.m_pipelineLayout)
    , m_vertexBuffer(other.m_vertexBuffer)
    , m_vertexMemory(other.m_vertexMemory)
    , m_daySkyboxImage(other.m_daySkyboxImage)
    , m_daySkyboxMemory(other.m_daySkyboxMemory)
    , m_daySkyboxView(other.m_daySkyboxView)
    , m_nightSkyboxImage(other.m_nightSkyboxImage)
    , m_nightSkyboxMemory(other.m_nightSkyboxMemory)
    , m_nightSkyboxView(other.m_nightSkyboxView)
    , m_sampler(other.m_sampler)
    , m_initialized(other.m_initialized) {
    // Invalidate source
    other.m_pipeline = VK_NULL_HANDLE;
    other.m_vertexBuffer = VK_NULL_HANDLE;
    other.m_vertexMemory = VK_NULL_HANDLE;
    other.m_daySkyboxImage = VK_NULL_HANDLE;
    other.m_daySkyboxMemory = VK_NULL_HANDLE;
    other.m_daySkyboxView = VK_NULL_HANDLE;
    other.m_nightSkyboxImage = VK_NULL_HANDLE;
    other.m_nightSkyboxMemory = VK_NULL_HANDLE;
    other.m_nightSkyboxView = VK_NULL_HANDLE;
    other.m_sampler = VK_NULL_HANDLE;
    other.m_initialized = false;
}

SkyboxRenderer& SkyboxRenderer::operator=(SkyboxRenderer&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = other.m_device;
        m_physicalDevice = other.m_physicalDevice;
        m_commandPool = other.m_commandPool;
        m_graphicsQueue = other.m_graphicsQueue;
        m_pipeline = other.m_pipeline;
        m_pipelineLayout = other.m_pipelineLayout;
        m_vertexBuffer = other.m_vertexBuffer;
        m_vertexMemory = other.m_vertexMemory;
        m_daySkyboxImage = other.m_daySkyboxImage;
        m_daySkyboxMemory = other.m_daySkyboxMemory;
        m_daySkyboxView = other.m_daySkyboxView;
        m_nightSkyboxImage = other.m_nightSkyboxImage;
        m_nightSkyboxMemory = other.m_nightSkyboxMemory;
        m_nightSkyboxView = other.m_nightSkyboxView;
        m_sampler = other.m_sampler;
        m_initialized = other.m_initialized;

        // Invalidate source
        other.m_pipeline = VK_NULL_HANDLE;
        other.m_vertexBuffer = VK_NULL_HANDLE;
        other.m_vertexMemory = VK_NULL_HANDLE;
        other.m_daySkyboxImage = VK_NULL_HANDLE;
        other.m_daySkyboxMemory = VK_NULL_HANDLE;
        other.m_daySkyboxView = VK_NULL_HANDLE;
        other.m_nightSkyboxImage = VK_NULL_HANDLE;
        other.m_nightSkyboxMemory = VK_NULL_HANDLE;
        other.m_nightSkyboxView = VK_NULL_HANDLE;
        other.m_sampler = VK_NULL_HANDLE;
        other.m_initialized = false;
    }
    return *this;
}

// ============================================================================
// Initialization / Cleanup
// ============================================================================

void SkyboxRenderer::initialize(VkRenderPass renderPass, VkPipelineLayout pipelineLayout,
                                VkExtent2D extent) {
    if (m_initialized) {
        return;
    }

    m_pipelineLayout = pipelineLayout;

    // Generate procedural cubemaps
    createProceduralDayCubeMap();
    createProceduralNightCubeMap();

    // Batch the final transition for both cube maps into a single vkCmdPipelineBarrier call
    // Optimization: Single command buffer submission for multiple transitions (50% reduction)
    std::cout << "Batching cube map transitions for performance..." << '\n';
    batchTransitionImageLayouts(
        {m_daySkyboxImage, m_nightSkyboxImage},
        {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB},
        {6, 6},
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // Create cube map views (after transition to SHADER_READ_ONLY_OPTIMAL)
    m_daySkyboxView = createCubeMapView(m_daySkyboxImage, VK_FORMAT_R8G8B8A8_SRGB);
    m_nightSkyboxView = createCubeMapView(m_nightSkyboxImage, VK_FORMAT_R8G8B8A8_SRGB);

    // Create cube map sampler (shared by both skyboxes)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create skybox sampler!");
    }

    // Create vertex buffer
    createVertexBuffer();

    // Create pipeline
    createPipeline(renderPass, extent);

    m_initialized = true;
    std::cout << "Skybox renderer initialized" << '\n';
}

void SkyboxRenderer::cleanup() {
    if (!m_initialized && m_pipeline == VK_NULL_HANDLE) {
        return;
    }

    std::cout << "    Cleaning up skybox renderer..." << '\n';

    // Wait for device to be idle before cleanup
    vkDeviceWaitIdle(m_device);

    // Cleanup pipeline
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    // Cleanup vertex buffer
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_vertexMemory, nullptr);
        m_vertexMemory = VK_NULL_HANDLE;
    }

    // Cleanup sampler
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    // Cleanup day skybox
    if (m_daySkyboxView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_daySkyboxView, nullptr);
        m_daySkyboxView = VK_NULL_HANDLE;
    }
    if (m_daySkyboxImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_daySkyboxImage, nullptr);
        m_daySkyboxImage = VK_NULL_HANDLE;
    }
    if (m_daySkyboxMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_daySkyboxMemory, nullptr);
        m_daySkyboxMemory = VK_NULL_HANDLE;
    }

    // Cleanup night skybox
    if (m_nightSkyboxView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_nightSkyboxView, nullptr);
        m_nightSkyboxView = VK_NULL_HANDLE;
    }
    if (m_nightSkyboxImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_nightSkyboxImage, nullptr);
        m_nightSkyboxImage = VK_NULL_HANDLE;
    }
    if (m_nightSkyboxMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_nightSkyboxMemory, nullptr);
        m_nightSkyboxMemory = VK_NULL_HANDLE;
    }

    m_initialized = false;
}

// ============================================================================
// Loading (Stubs for future implementation)
// ============================================================================

void SkyboxRenderer::loadCubemap(const std::array<std::string, 6>& /*facePaths*/) {
    // TODO: Implement file-based cubemap loading
    // For now, using procedural generation
    std::cout << "loadCubemap: Using procedural generation instead" << '\n';
}

void SkyboxRenderer::loadEquirectangular(const std::string& /*path*/) {
    // TODO: Implement equirectangular to cubemap conversion
    // For now, using procedural generation
    std::cout << "loadEquirectangular: Using procedural generation instead" << '\n';
}

// ============================================================================
// Pipeline
// ============================================================================

void SkyboxRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    // Skybox uses vec3 positions only
    VkVertexInputBindingDescription skyboxBinding{};
    skyboxBinding.binding = 0;
    skyboxBinding.stride = sizeof(glm::vec3);
    skyboxBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> skyboxAttrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}  // Position only
    };

    // Build skybox pipeline using PipelineBuilder
    PipelineBuilder builder(m_device, renderPass);
    m_pipeline = builder
        .setShaders("shaders/skybox_vert.spv", "shaders/skybox_frag.spv")
        .setVertexInput(skyboxBinding, skyboxAttrs)
        .setViewport(static_cast<float>(extent.width),
                     static_cast<float>(extent.height))
        .setNoCull()
        .setDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL)  // Skybox at max depth
        .setNoBlending()
        .setPipelineLayout(m_pipelineLayout)
        .build();
    builder.destroyShaderModules();

    std::cout << "Created skybox pipeline" << '\n';
}

void SkyboxRenderer::recreate(VkRenderPass renderPass, VkExtent2D extent) {
    // Destroy old pipeline
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    // Create new pipeline
    createPipeline(renderPass, extent);
}

// ============================================================================
// Drawing
// ============================================================================

void SkyboxRenderer::draw(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor) {
    // Bind skybox pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Bind descriptor set (contains UBO and cube maps)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           m_pipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);

    // Bind skybox vertex buffer
    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

    // Draw skybox cube (36 vertices)
    vkCmdDraw(cmd, 36, 1, 0, 0);
}

// ============================================================================
// Procedural Day CubeMap
// ============================================================================

void SkyboxRenderer::createProceduralDayCubeMap() {
    const uint32_t size = 256;  // 256x256 per face
    const uint32_t faceSize = size * size * 4;  // RGBA
    const uint32_t totalSize = faceSize * 6;

    // Allocate buffer for all 6 faces
    std::vector<unsigned char> pixels(totalSize);

    // Generate gradient for each face
    // Face order: +X, -X, +Y, -Y, +Z, -Z
    for (int face = 0; face < 6; face++) {
        unsigned char* faceData = pixels.data() + (face * faceSize);

        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                uint32_t index = (y * size + x) * 4;

                // Normalized coordinates [0, 1]
                float v = static_cast<float>(y) / static_cast<float>(size - 1);

                // Create gradient from top (zenith) to bottom (horizon)
                float t = v;  // 0 at top, 1 at bottom

                // Sky colors - natural blue sky gradient (neutral, not overly saturated)
                glm::vec3 zenithColor = glm::vec3(0.25f, 0.5f, 0.85f);    // Deep blue at zenith
                glm::vec3 horizonColor = glm::vec3(0.65f, 0.8f, 0.95f);   // Light blue/white at horizon

                // Interpolate based on vertical position with slight curve
                float curvedT = t * t;  // Quadratic falloff for smoother gradient
                glm::vec3 skyColor = glm::mix(zenithColor, horizonColor, curvedT);

                // For +Y face (top), make it uniform zenith color
                // For -Y face (bottom), fade to darker blue
                if (face == 2) {  // +Y (top)
                    skyColor = zenithColor;
                } else if (face == 3) {  // -Y (bottom)
                    // Below horizon - slightly darker blue
                    glm::vec3 belowColor = glm::vec3(0.4f, 0.6f, 0.75f);
                    skyColor = glm::mix(horizonColor, belowColor, t);
                }

                // Write pixel (convert from [0,1] to [0,255])
                faceData[index + 0] = static_cast<unsigned char>(skyColor.r * 255.0f);
                faceData[index + 1] = static_cast<unsigned char>(skyColor.g * 255.0f);
                faceData[index + 2] = static_cast<unsigned char>(skyColor.b * 255.0f);
                faceData[index + 3] = 255;  // Alpha
            }
        }
    }

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);

    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, totalSize, 0, &data);
    memcpy(data, pixels.data(), totalSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create cube map image
    createCubeMap(size, size, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_daySkyboxImage, m_daySkyboxMemory);

    // Transition to transfer dst
    transitionCubeMapLayout(m_daySkyboxImage, VK_FORMAT_R8G8B8A8_SRGB,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy each face
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    for (uint32_t face = 0; face < 6; face++) {
        VkBufferImageCopy region{};
        region.bufferOffset = face * faceSize;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {size, size, 1};

        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_daySkyboxImage,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    endSingleTimeCommands(commandBuffer);

    // Final transition deferred - will batch with night skybox in initialize()

    // Cleanup staging buffer
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    std::cout << "Created procedural day cube map skybox (256x256 per face)" << '\n';
}

// ============================================================================
// Procedural Night CubeMap
// ============================================================================

void SkyboxRenderer::createProceduralNightCubeMap() {
    const uint32_t size = 256;  // 256x256 per face
    const uint32_t faceSize = size * size * 4;  // RGBA
    const uint32_t totalSize = faceSize * 6;

    // Allocate buffer for all 6 faces
    std::vector<unsigned char> pixels(totalSize);

    // Generate very dark gradient for each face
    // Face order: +X, -X, +Y, -Y, +Z, -Z
    for (int face = 0; face < 6; face++) {
        unsigned char* faceData = pixels.data() + (face * faceSize);

        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                uint32_t index = (y * size + x) * 4;

                // Normalized coordinates [0, 1]
                float v = static_cast<float>(y) / static_cast<float>(size - 1);

                // Create gradient from top to bottom
                float t = v;  // 0 at top, 1 at bottom

                // Very dark night colors - nearly pure black, no tint
                glm::vec3 zenithColor = glm::vec3(0.01f, 0.01f, 0.01f);   // Almost pure black at top
                glm::vec3 horizonColor = glm::vec3(0.03f, 0.03f, 0.03f); // Slightly lighter at horizon

                // Interpolate based on vertical position
                glm::vec3 skyColor = glm::mix(zenithColor, horizonColor, t);

                // For +Y face (top), keep it uniform
                // For -Y face (bottom), make it even darker
                if (face == 2) {  // +Y (top)
                    skyColor = zenithColor;
                } else if (face == 3) {  // -Y (bottom)
                    glm::vec3 groundColor = glm::vec3(0.005f, 0.005f, 0.005f);  // Nearly pure black
                    skyColor = glm::mix(horizonColor, groundColor, t);
                }

                // Write pixel (convert from [0,1] to [0,255])
                faceData[index + 0] = static_cast<unsigned char>(skyColor.r * 255.0f);
                faceData[index + 1] = static_cast<unsigned char>(skyColor.g * 255.0f);
                faceData[index + 2] = static_cast<unsigned char>(skyColor.b * 255.0f);
                faceData[index + 3] = 255;  // Alpha
            }
        }
    }

    // Add stars to the night sky as texture pixels
    // Simple hash function for deterministic randomness
    auto hash = [](uint32_t x, uint32_t y, uint32_t face) -> uint32_t {
        uint32_t h = x * 374761393u + y * 668265263u + face * 1610612741u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    };

    // Add stars to each face (skip -Y face which is below horizon)
    for (int face = 0; face < 6; face++) {
        if (face == 3) continue;  // Skip -Y (bottom face)

        unsigned char* faceData = pixels.data() + (face * faceSize);

        // Scatter stars across the face
        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                uint32_t h = hash(x, y, face);
                float randVal = static_cast<float>(h % 10000) / 10000.0f;

                // 0.75% chance of a star at this pixel
                if (randVal < 0.0075f) {
                    uint32_t index = (y * size + x) * 4;

                    // Determine star color based on hash
                    float colorRand = static_cast<float>((h >> 8) % 1000) / 1000.0f;
                    glm::vec3 starColor;

                    if (colorRand < 0.15f) {
                        starColor = glm::vec3(1.0f, 0.6f, 0.6f);  // Red star
                    } else if (colorRand < 0.30f) {
                        starColor = glm::vec3(0.6f, 0.7f, 1.0f);  // Blue star
                    } else {
                        starColor = glm::vec3(0.95f, 0.95f, 1.0f);  // White star
                    }

                    // Vary brightness
                    float brightness = 0.7f + 0.3f * (static_cast<float>((h >> 16) % 1000) / 1000.0f);
                    starColor *= brightness;

                    // Write star pixel
                    faceData[index + 0] = static_cast<unsigned char>(glm::clamp(starColor.r * 255.0f, 0.0f, 255.0f));
                    faceData[index + 1] = static_cast<unsigned char>(glm::clamp(starColor.g * 255.0f, 0.0f, 255.0f));
                    faceData[index + 2] = static_cast<unsigned char>(glm::clamp(starColor.b * 255.0f, 0.0f, 255.0f));
                    faceData[index + 3] = 255;
                }
            }
        }
    }

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);

    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, totalSize, 0, &data);
    memcpy(data, pixels.data(), totalSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create cube map image
    createCubeMap(size, size, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_nightSkyboxImage, m_nightSkyboxMemory);

    // Transition to transfer dst
    transitionCubeMapLayout(m_nightSkyboxImage, VK_FORMAT_R8G8B8A8_SRGB,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy each face
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    for (uint32_t face = 0; face < 6; face++) {
        VkBufferImageCopy region{};
        region.bufferOffset = face * faceSize;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {size, size, 1};

        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_nightSkyboxImage,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    endSingleTimeCommands(commandBuffer);

    // Final transition deferred - will batch with day skybox in initialize()

    // Cleanup staging buffer
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    std::cout << "Created procedural night cube map skybox (256x256 per face)" << '\n';
}

// ============================================================================
// CubeMap Helpers
// ============================================================================

void SkyboxRenderer::createCubeMap(uint32_t width, uint32_t height, VkFormat format,
                                   VkImageTiling tiling, VkImageUsageFlags usage,
                                   VkMemoryPropertyFlags properties,
                                   VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;  // 6 faces for cube map
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;  // Important for cube maps!

    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cube map image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate cube map memory!");
    }

    vkBindImageMemory(m_device, image, imageMemory, 0);
}

VkImageView SkyboxRenderer::createCubeMapView(VkImage image, VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;  // Cube map view type
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;  // All 6 faces

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cube map image view!");
    }

    return imageView;
}

void SkyboxRenderer::transitionCubeMapLayout(VkImage image, VkFormat /*format*/,
                                             VkImageLayout oldLayout,
                                             VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;  // All 6 cube map faces

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("unsupported cube map layout transition!");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(commandBuffer);
}

void SkyboxRenderer::batchTransitionImageLayouts(const std::vector<VkImage>& images,
                                                 const std::vector<VkFormat>& /*formats*/,
                                                 const std::vector<uint32_t>& layerCounts,
                                                 VkImageLayout oldLayout,
                                                 VkImageLayout newLayout) {
    if (images.empty()) return;

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // Create barrier for each image
    std::vector<VkImageMemoryBarrier> barriers(images.size());

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    // Determine pipeline stages and access masks based on layouts
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        for (size_t i = 0; i < images.size(); i++) {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].pNext = nullptr;
            barriers[i].oldLayout = oldLayout;
            barriers[i].newLayout = newLayout;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = images[i];
            barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[i].subresourceRange.baseMipLevel = 0;
            barriers[i].subresourceRange.levelCount = 1;
            barriers[i].subresourceRange.baseArrayLayer = 0;
            barriers[i].subresourceRange.layerCount = layerCounts[i];
            barriers[i].srcAccessMask = 0;
            barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        for (size_t i = 0; i < images.size(); i++) {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].pNext = nullptr;
            barriers[i].oldLayout = oldLayout;
            barriers[i].newLayout = newLayout;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = images[i];
            barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[i].subresourceRange.baseMipLevel = 0;
            barriers[i].subresourceRange.levelCount = 1;
            barriers[i].subresourceRange.baseArrayLayer = 0;
            barriers[i].subresourceRange.layerCount = layerCounts[i];
            barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
    } else {
        throw std::invalid_argument("unsupported batch layout transition!");
    }

    // Single vkCmdPipelineBarrier call for all images - much more efficient!
    vkCmdPipelineBarrier(commandBuffer,
                         sourceStage, destinationStage,
                         0,
                         0, nullptr,
                         0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());

    endSingleTimeCommands(commandBuffer);
}

// ============================================================================
// Vertex Buffer
// ============================================================================

void SkyboxRenderer::createVertexBuffer() {
    // Skybox cube vertices (positions only, no UVs - we use direction for sampling)
    // Large cube centered at origin
    std::vector<glm::vec3> skyboxVertices = {
        // Positions
        {-1.0f,  1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f},

        {-1.0f, -1.0f,  1.0f},
        {-1.0f, -1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f,  1.0f},
        {-1.0f, -1.0f,  1.0f},

        { 1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},

        {-1.0f, -1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        { 1.0f, -1.0f,  1.0f},
        {-1.0f, -1.0f,  1.0f},

        {-1.0f,  1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f},
        { 1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f},
        {-1.0f,  1.0f, -1.0f},

        {-1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f},
        { 1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f},
        { 1.0f, -1.0f,  1.0f}
    };

    VkDeviceSize bufferSize = sizeof(glm::vec3) * skyboxVertices.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, skyboxVertices.data(), bufferSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create vertex buffer
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_vertexBuffer, m_vertexMemory);

    copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    std::cout << "Created skybox geometry" << '\n';
}

// ============================================================================
// Buffer Helpers
// ============================================================================

void SkyboxRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
}

void SkyboxRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

uint32_t SkyboxRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

// ============================================================================
// Command Buffer Helpers
// ============================================================================

VkCommandBuffer SkyboxRenderer::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void SkyboxRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}
