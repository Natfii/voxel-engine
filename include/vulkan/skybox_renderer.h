/**
 * @file skybox_renderer.h
 * @brief Skybox rendering with day/night cycle support
 *
 * Encapsulates skybox-specific rendering including procedural cubemap generation,
 * day/night skybox blending, and efficient batched pipeline barriers.
 *
 * Features:
 * - Procedural day sky cubemap generation (blue gradient)
 * - Procedural night sky cubemap with stars
 * - Day/night blending in shader
 * - Optimized batched image layout transitions
 *
 * Usage:
 *   SkyboxRenderer skybox(device, physicalDevice, commandPool, graphicsQueue);
 *   skybox.initialize(renderPass, globalLayout, extent);
 *
 *   // In render loop:
 *   skybox.draw(commandBuffer, globalDescriptor, viewProj);
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <array>
#include <vector>

/**
 * @brief Manages skybox rendering with day/night cycle support
 *
 * Provides procedural cubemap generation for both day and night skies,
 * with efficient batched transitions and shared sampler resources.
 */
class SkyboxRenderer {
public:
    /**
     * @brief Construct a new Skybox Renderer
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device (for memory queries)
     * @param commandPool Command pool for single-time commands
     * @param graphicsQueue Queue for command submission
     */
    SkyboxRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkCommandPool commandPool, VkQueue graphicsQueue);

    /**
     * @brief Destructor - calls cleanup() if not already called
     */
    ~SkyboxRenderer();

    // Prevent copying
    SkyboxRenderer(const SkyboxRenderer&) = delete;
    SkyboxRenderer& operator=(const SkyboxRenderer&) = delete;

    // Allow moving
    SkyboxRenderer(SkyboxRenderer&& other) noexcept;
    SkyboxRenderer& operator=(SkyboxRenderer&& other) noexcept;

    /**
     * @brief Initialize skybox resources
     *
     * Creates procedural cubemaps, vertex buffer, sampler, and pipeline.
     * Must be called before draw().
     *
     * @param renderPass Render pass for pipeline creation
     * @param pipelineLayout Pipeline layout to use (must have cubemap bindings at 2 and 3)
     * @param extent Swapchain extent for viewport
     */
    void initialize(VkRenderPass renderPass, VkPipelineLayout pipelineLayout,
                   VkExtent2D extent);

    /**
     * @brief Clean up all Vulkan resources
     *
     * Safe to call multiple times. Called automatically by destructor.
     */
    void cleanup();

    /**
     * @brief Load cubemap from 6 face images
     *
     * Face order: +X, -X, +Y, -Y, +Z, -Z
     * NOTE: Currently uses procedural generation instead.
     *
     * @param facePaths Paths to the 6 face image files
     */
    void loadCubemap(const std::array<std::string, 6>& facePaths);

    /**
     * @brief Load cubemap from equirectangular HDR image
     *
     * NOTE: Currently uses procedural generation instead.
     *
     * @param path Path to equirectangular image
     */
    void loadEquirectangular(const std::string& path);

    /**
     * @brief Create skybox rendering pipeline
     *
     * Called automatically by initialize(). Can be called again
     * after recreate() for swapchain resize.
     *
     * @param renderPass Render pass for pipeline creation
     * @param extent Swapchain extent for viewport
     */
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);

    /**
     * @brief Record skybox draw commands
     *
     * Binds pipeline, descriptor set, vertex buffer, and issues draw call.
     * Must be called within an active render pass.
     *
     * @param cmd Command buffer to record into
     * @param globalDescriptor Descriptor set containing UBO and cubemap bindings
     */
    void draw(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor);

    /**
     * @brief Recreate pipeline for swapchain resize
     *
     * Destroys old pipeline and creates new one with updated extent.
     *
     * @param renderPass New render pass
     * @param extent New swapchain extent
     */
    void recreate(VkRenderPass renderPass, VkExtent2D extent);

    // ========== Accessors for descriptor set updates ==========

    /**
     * @brief Get day skybox image view for descriptor binding
     * @return Day skybox cubemap image view
     */
    VkImageView getDaySkyboxView() const { return m_daySkyboxView; }

    /**
     * @brief Get night skybox image view for descriptor binding
     * @return Night skybox cubemap image view
     */
    VkImageView getNightSkyboxView() const { return m_nightSkyboxView; }

    /**
     * @brief Get shared cubemap sampler for descriptor binding
     * @return Cubemap sampler
     */
    VkSampler getSampler() const { return m_sampler; }

    /**
     * @brief Get skybox pipeline
     * @return Skybox graphics pipeline
     */
    VkPipeline getPipeline() const { return m_pipeline; }

private:
    // ========== Cubemap Creation ==========

    /**
     * @brief Generate procedural day sky cubemap
     *
     * Creates a 256x256 per-face cubemap with blue sky gradient.
     * Zenith is deep blue, horizon is light blue.
     */
    void createProceduralDayCubeMap();

    /**
     * @brief Generate procedural night sky cubemap
     *
     * Creates a 256x256 per-face cubemap with dark sky and stars.
     * Uses deterministic hash for consistent star placement.
     */
    void createProceduralNightCubeMap();

    /**
     * @brief Create cubemap image and allocate memory
     *
     * @param width Face width
     * @param height Face height
     * @param format Image format
     * @param tiling Image tiling mode
     * @param usage Image usage flags
     * @param properties Memory property flags
     * @param image Output image handle
     * @param imageMemory Output memory handle
     */
    void createCubeMap(uint32_t width, uint32_t height, VkFormat format,
                       VkImageTiling tiling, VkImageUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       VkImage& image, VkDeviceMemory& imageMemory);

    /**
     * @brief Create cubemap image view
     *
     * @param image Cubemap image
     * @param format Image format
     * @return Created image view
     */
    VkImageView createCubeMapView(VkImage image, VkFormat format);

    /**
     * @brief Transition cubemap image layout
     *
     * @param image Image to transition
     * @param format Image format
     * @param oldLayout Current layout
     * @param newLayout Target layout
     */
    void transitionCubeMapLayout(VkImage image, VkFormat format,
                                 VkImageLayout oldLayout, VkImageLayout newLayout);

    /**
     * @brief Batch multiple image layout transitions
     *
     * Optimization: Single command buffer submission for multiple transitions.
     *
     * @param images Images to transition
     * @param formats Image formats
     * @param layerCounts Layer counts per image (6 for cubemaps)
     * @param oldLayout Current layout
     * @param newLayout Target layout
     */
    void batchTransitionImageLayouts(const std::vector<VkImage>& images,
                                     const std::vector<VkFormat>& formats,
                                     const std::vector<uint32_t>& layerCounts,
                                     VkImageLayout oldLayout,
                                     VkImageLayout newLayout);

    // ========== Vertex Buffer ==========

    /**
     * @brief Create skybox cube vertex buffer
     *
     * Creates a unit cube with 36 vertices (6 faces * 2 triangles * 3 vertices).
     * Only positions are stored; texture coords derived from direction in shader.
     */
    void createVertexBuffer();

    // ========== Helper Functions ==========

    /**
     * @brief Create a Vulkan buffer with memory
     */
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    /**
     * @brief Copy data between buffers
     */
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    /**
     * @brief Find suitable memory type
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /**
     * @brief Begin single-time command buffer
     */
    VkCommandBuffer beginSingleTimeCommands();

    /**
     * @brief End and submit single-time command buffer
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    // ========== Vulkan Handles (External) ==========
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkCommandPool m_commandPool;
    VkQueue m_graphicsQueue;

    // ========== Pipeline ==========
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;  // Borrowed from VulkanRenderer

    // ========== Vertex Buffer ==========
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;

    // ========== Day Skybox Cubemap ==========
    VkImage m_daySkyboxImage = VK_NULL_HANDLE;
    VkDeviceMemory m_daySkyboxMemory = VK_NULL_HANDLE;
    VkImageView m_daySkyboxView = VK_NULL_HANDLE;

    // ========== Night Skybox Cubemap ==========
    VkImage m_nightSkyboxImage = VK_NULL_HANDLE;
    VkDeviceMemory m_nightSkyboxMemory = VK_NULL_HANDLE;
    VkImageView m_nightSkyboxView = VK_NULL_HANDLE;

    // ========== Shared Sampler ==========
    VkSampler m_sampler = VK_NULL_HANDLE;

    // ========== State ==========
    bool m_initialized = false;
};
