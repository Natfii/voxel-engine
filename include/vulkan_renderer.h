/**
 * @file vulkan_renderer.h
 * @brief Vulkan rendering backend with modern graphics pipeline
 *
 */

#pragma once

// ========== GPU OPTIMIZATION FLAGS ==========
// Enable indirect drawing (reduces 300+ draw calls to 2 per frame)
// Set to 1 to enable, 0 to use legacy per-chunk drawing
#define USE_INDIRECT_DRAWING 1

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <array>
#include <deque>
#include <mutex>

// Forward declare Vertex (defined in chunk.h)
struct Vertex;

/**
 * @brief Uniform buffer object for shader parameters
 *
 * Passed to shaders every frame. Layout must match std140 in GLSL.
 * Alignment directives ensure proper GPU memory layout.
 */
struct UniformBufferObject {
    alignas(16) glm::mat4 model;         ///< Model transformation matrix
    alignas(16) glm::mat4 view;          ///< View (camera) matrix
    alignas(16) glm::mat4 projection;    ///< Projection matrix
    alignas(16) glm::vec4 cameraPos;     ///< Camera position (.xyz) + render distance (.w)
    alignas(16) glm::vec4 skyTimeData;   ///< Time data (.x=time 0-1, .y=sun, .z=moon, .w=underwater 0/1)
    alignas(16) glm::vec4 liquidFogColor;///< Liquid fog color (.rgb) + density (.a)
    alignas(16) glm::vec4 liquidFogDist; ///< Fog distances (.x=start, .y=end) + unused (.zw)
    alignas(16) glm::vec4 liquidTint;    ///< Liquid tint color (.rgb) + darken factor (.a)
    alignas(16) glm::vec4 atlasInfo;     ///< Texture atlas info (.x=width in cells, .y=height, .z=cell size, .w=unused)
};

/**
 * @brief Queue family indices for Vulkan device
 *
 * Identifies which queue families support graphics, presentation, and transfer.
 * PERFORMANCE OPTIMIZATION (2025-11-24): Added dedicated transfer queue for async uploads
 */
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;  ///< Queue family supporting graphics ops
    std::optional<uint32_t> presentFamily;   ///< Queue family supporting presentation
    std::optional<uint32_t> transferFamily;  ///< Queue family for async transfers (optional)

    /**
     * @brief Checks if all required queue families are available
     * @return True if both graphics and present queues exist
     * Note: Transfer queue is optional - will fall back to graphics queue if unavailable
     */
    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }

    /**
     * @brief Checks if a dedicated transfer queue is available
     * @return True if transfer queue exists and differs from graphics queue
     */
    bool hasDedicatedTransferQueue() {
        return transferFamily.has_value() && transferFamily.value() != graphicsFamily.value();
    }
};

/**
 * @brief Swapchain capabilities and supported formats
 *
 * Query result from Vulkan physical device containing available
 * swapchain configurations.
 */
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;        ///< Surface capabilities (min/max images, extents)
    std::vector<VkSurfaceFormatKHR> formats;      ///< Supported color formats
    std::vector<VkPresentModeKHR> presentModes;   ///< Supported presentation modes
};

// ========== Staging Buffer Pool (2025-11-25) ==========
// Persistent staging buffer pool for efficient GPU uploads
// Buffers are mapped once and reused, eliminating per-transfer map/unmap overhead

/**
 * @brief A single staging buffer with persistent mapping
 */
struct StagingBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;   ///< Persistently mapped - never unmapped
    VkDeviceSize size = 0;
    bool inUse = false;
};

/**
 * @brief Pool of persistently mapped staging buffers
 *
 * Eliminates vkMapMemory/vkUnmapMemory overhead by mapping buffers once at creation.
 * Use acquire() to get a buffer, memcpy data, then release() when done.
 */
class StagingBufferPool {
public:
    StagingBufferPool() = default;
    ~StagingBufferPool() = default;

    /**
     * @brief Initialize the pool with pre-allocated buffers
     * @param device Vulkan device
     * @param physicalDevice Physical device for memory type lookup
     * @param bufferSize Size of each staging buffer
     * @param count Number of buffers to create
     */
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize bufferSize, size_t count);

    /**
     * @brief Cleanup all buffers
     * @param device Vulkan device
     */
    void cleanup(VkDevice device);

    /**
     * @brief Acquire an available staging buffer
     * @return Pointer to staging buffer, or nullptr if all in use
     */
    StagingBuffer* acquire();

    /**
     * @brief Acquire a buffer of at least the specified size
     * @param minSize Minimum required size
     * @return Pointer to staging buffer, or nullptr if none available
     */
    StagingBuffer* acquireWithSize(VkDeviceSize minSize);

    /**
     * @brief Release a staging buffer back to the pool
     * @param buffer Buffer to release
     */
    void release(StagingBuffer* buffer);

    /**
     * @brief Get total number of buffers in pool
     */
    size_t getPoolSize() const { return m_pool.size(); }

    /**
     * @brief Get number of buffers currently in use
     */
    size_t getInUseCount() const;

private:
    std::vector<StagingBuffer> m_pool;
    std::mutex m_poolMutex;
};

/**
 * @brief Vulkan rendering backend with complete graphics pipeline
 *
 * The VulkanRenderer class manages all Vulkan resources and rendering operations:
 * - Instance, device, and surface creation
 * - Swapchain management with automatic recreation on resize
 * - Multiple graphics pipelines (standard, wireframe, lines, skybox)
 * - Descriptor sets for uniforms and textures
 * - Command buffer recording and submission
 * - Synchronization with semaphores and fences
 * - Sky system with day/night cube maps
 *
 * Frame Rendering Flow:
 * 1. beginFrame() - Acquires swapchain image, begins command buffer
 * 2. Render operations (world, skybox, UI)
 * 3. endFrame() - Submits commands, presents image
 *
 * Pipeline Features:
 * - Standard pipeline: Textured voxel rendering with depth testing
 * - Wireframe pipeline: Debug visualization of chunk boundaries
 * - Line pipeline: Block outlines and targeting visualization
 * - Skybox pipeline: Cube map rendering with time-based blending
 *
 * Resource Management:
 * - Automatic swapchain recreation on window resize
 * - Double/triple buffering for smooth frame pacing
 * - Validation layers in debug builds
 *
 * @note This class is not copyable. There should be only one renderer instance.
 */
class VulkanRenderer {
public:
    /**
     * @brief Constructs the Vulkan renderer and initializes all resources
     *
     * Creates Vulkan instance, selects physical device, creates logical device,
     * sets up swapchain, pipelines, and all required resources.
     *
     * @param window GLFW window to render to
     * @throws std::runtime_error if Vulkan initialization fails
     */
    VulkanRenderer(GLFWwindow* window);

    /**
     * @brief Destroys the renderer and cleans up all Vulkan resources
     *
     * Waits for device idle, then destroys all Vulkan objects in reverse order.
     */
    ~VulkanRenderer();

    // Prevent copying
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // ========== Core Rendering ==========

    /**
     * @brief Begins a new frame and acquires a swapchain image
     *
     * Waits for previous frame fence, acquires next swapchain image,
     * and begins recording the command buffer.
     *
     * @return True if frame should be rendered, false if swapchain needs recreation
     */
    bool beginFrame();

    /**
     * @brief Ends the frame and presents the image
     *
     * Ends command buffer recording, submits to graphics queue,
     * and presents the image to the screen.
     */
    void endFrame();

    /**
     * @brief Waits for all GPU work to complete
     *
     * Blocks until the GPU finishes processing all submitted commands.
     * Used during initialization to ensure smooth gameplay start.
     */
    void waitForGPUIdle();

    /**
     * @brief Updates uniform buffer with current frame data and liquid properties
     *
     * Uploads MVP matrices, camera position, render distance, and liquid properties to GPU.
     *
     * @param currentImage Swapchain image index
     * @param model Model transformation matrix
     * @param view View (camera) matrix
     * @param projection Projection matrix
     * @param cameraPos Camera position in world space
     * @param renderDistance Maximum render distance for fog
     * @param underwater Whether camera is submerged in liquid
     * @param liquidFogColor Liquid fog color (RGB)
     * @param liquidFogStart Distance where fog starts
     * @param liquidFogEnd Distance where fog is fully opaque
     * @param liquidTintColor Liquid tint color applied when submerged (RGB)
     * @param liquidDarkenFactor How much darker it gets underwater (0-1)
     */
    void updateUniformBuffer(uint32_t currentImage, const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos, float renderDistance, bool underwater = false,
                            const glm::vec3& liquidFogColor = glm::vec3(0.1f, 0.3f, 0.5f), float liquidFogStart = 1.0f, float liquidFogEnd = 8.0f,
                            const glm::vec3& liquidTintColor = glm::vec3(0.4f, 0.7f, 1.0f), float liquidDarkenFactor = 0.4f);

    // ========== Sky System ==========

    /**
     * @brief Sets the time of day for sky rendering
     *
     * Controls day/night cycle and celestial object visibility.
     *
     * @param timeOfDay Time value (0.0-1.0): 0=midnight, 0.25=dawn, 0.5=noon, 0.75=dusk
     */
    void setSkyTime(float timeOfDay);

    /**
     * @brief Renders the skybox cube map
     *
     * Should be called before rendering the world. Blends between day and night
     * cube maps based on current sky time.
     */
    void renderSkybox();

    // ========== Texture Management ==========

    /**
     * @brief Binds texture atlas to descriptor sets
     *
     * Updates descriptor sets to use the block texture atlas.
     * Call after loading all block textures.
     *
     * @param atlasView Image view of the texture atlas
     * @param atlasSampler Sampler for the texture atlas
     */
    void bindAtlasTexture(VkImageView atlasView, VkSampler atlasSampler);

    // ========== Vulkan Object Getters ==========
    // Provide access to Vulkan objects for chunk/world rendering

    VkInstance getInstance() const { return m_instance; }                       ///< Get Vulkan instance
    VkDevice getDevice() const { return m_device; }                             ///< Get logical device
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }     ///< Get physical device
    VkCommandPool getCommandPool() const { return m_commandPool; }              ///< Get command pool
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }                ///< Get graphics queue
    VkRenderPass getRenderPass() const { return m_renderPass; }                 ///< Get render pass
    VkPipeline getGraphicsPipeline() const { return m_graphicsPipeline; }       ///< Get standard pipeline
    VkPipeline getWireframePipeline() const { return m_wireframePipeline; }     ///< Get wireframe pipeline
    VkPipeline getLinePipeline() const { return m_linePipeline; }               ///< Get line pipeline
    VkPipeline getTransparentPipeline() const { return m_transparentPipeline; } ///< Get transparent pipeline (no depth write)
    VkPipeline getMeshPipeline() const { return m_meshPipeline; }               ///< Get mesh rendering pipeline
    VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }     ///< Get pipeline layout
    VkPipelineLayout getMeshPipelineLayout() const { return m_meshPipelineLayout; }  ///< Get mesh pipeline layout
    VkCommandBuffer getCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }  ///< Get current cmd buffer
    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }  ///< Get descriptor layout
    VkDescriptorSet getCurrentDescriptorSet() const { return m_descriptorSets[m_currentFrame]; }  ///< Get current descriptor
    VkDescriptorPool getDescriptorPool() const { return m_descriptorPool; }  ///< Get descriptor pool

    /**
     * @brief Create a descriptor set for a custom texture (e.g., map preview on loading sphere)
     *
     * Creates a descriptor set matching the graphics pipeline layout but using
     * a custom texture instead of the texture atlas at binding 1.
     *
     * @param imageView The texture's image view
     * @param sampler The texture's sampler
     * @return VkDescriptorSet that can be bound during rendering
     */
    VkDescriptorSet createCustomTextureDescriptorSet(VkImageView imageView, VkSampler sampler);
    uint32_t getCurrentFrame() const { return m_currentFrame; }                 ///< Get current frame index
    VkExtent2D getSwapChainExtent() const { return m_swapChainExtent; }        ///< Get swapchain extent

    // ========== Window Events ==========

    /**
     * @brief Notifies renderer that framebuffer was resized
     *
     * Call from GLFW framebuffer size callback to trigger swapchain recreation.
     */
    void framebufferResized() { m_framebufferResized = true; }

    // ========== Buffer Utilities ==========
    // Public helper methods for creating buffers (used by chunks)

    /**
     * @brief Creates a Vulkan buffer with specified properties
     *
     * @param size Buffer size in bytes
     * @param usage Buffer usage flags (e.g., vertex buffer, index buffer)
     * @param properties Memory properties (e.g., device local, host visible)
     * @param buffer Output buffer handle
     * @param bufferMemory Output memory handle
     */
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    /**
     * @brief Copies data between two buffers
     *
     * @param srcBuffer Source buffer
     * @param dstBuffer Destination buffer
     * @param size Number of bytes to copy
     */
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    // ========== Batched Buffer Copying ==========

    /**
     * @brief Begins a batch of buffer copy operations
     *
     * All subsequent batchCopyBuffer() calls will be recorded into a single
     * command buffer. Call submitBufferCopyBatch() to execute all copies.
     *
     * Example:
     *   renderer->beginBufferCopyBatch();
     *   chunk1->createVertexBufferBatched(renderer);
     *   chunk2->createVertexBufferBatched(renderer);
     *   renderer->submitBufferCopyBatch();  // Submit all at once
     *   chunk1->cleanupStagingBuffers(renderer);
     *   chunk2->cleanupStagingBuffers(renderer);
     */
    void beginBufferCopyBatch();

    /**
     * @brief Records a buffer copy operation to the batch
     *
     * Must be called between beginBufferCopyBatch() and submitBufferCopyBatch().
     * Does not execute the copy immediately - just records it.
     *
     * @param srcBuffer Source buffer
     * @param dstBuffer Destination buffer
     * @param size Number of bytes to copy
     */
    void batchCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    /**
     * @brief Submits all batched buffer copies and waits for completion
     *
     * Executes all buffer copies recorded since beginBufferCopyBatch().
     * Blocks until GPU completes all copies.
     *
     * @param async If true, returns immediately without waiting (default: false)
     */
    void submitBufferCopyBatch(bool async = false);

    /**
     * @brief Begin an async chunk upload batch
     *
     * Like beginBufferCopyBatch(), but marks the batch as async.
     * Subsequent submitBufferCopyBatch() will return immediately.
     *
     * Pattern:
     *   renderer->beginAsyncChunkUpload();
     *   chunk->createVertexBufferBatched(renderer);
     *   renderer->submitAsyncChunkUpload(chunk);  // Non-blocking!
     */
    void beginAsyncChunkUpload();

    /**
     * @brief Submit async chunk upload batch
     *
     * Submits the batch without blocking. Staging buffers will be
     * cleaned up automatically when GPU completes the upload.
     *
     * @param chunk Chunk that was uploaded (for staging buffer tracking)
     */
    void submitAsyncChunkUpload(class Chunk* chunk);

    /**
     * @brief Begin a multi-chunk batched GPU upload
     *
     * Starts a batch that can contain multiple chunks' uploads.
     * All chunks will share a single command buffer and vkQueueSubmit.
     *
     * Pattern:
     *   renderer->beginBatchedChunkUploads();
     *   for (chunk : chunks) {
     *       renderer->addChunkToBatch(chunk);
     *   }
     *   renderer->submitBatchedChunkUploads();
     */
    void beginBatchedChunkUploads();

    /**
     * @brief Add a single chunk to the current batch
     *
     * Records this chunk's upload commands into the batch command buffer.
     * Does NOT submit - call submitBatchedChunkUploads() after all chunks added.
     *
     * @param chunk Chunk to upload
     */
    void addChunkToBatch(class Chunk* chunk);

    /**
     * @brief Submit all batched chunk uploads at once
     *
     * Submits a single vkQueueSubmit containing all chunks added via addChunkToBatch().
     * Staging buffers will be cleaned up asynchronously when GPU completes.
     */
    void submitBatchedChunkUploads();

    // ========== GPU Backlog Monitoring (2025-11-25) ==========

    /**
     * @brief Get number of pending async uploads waiting for GPU completion
     * @return Number of uploads in flight
     */
    size_t getPendingUploadCount() const;

    /**
     * @brief Check if GPU upload queue is backlogged
     *
     * Returns true if pending uploads exceed threshold, indicating
     * the GPU can't keep up with upload rate. Callers should defer
     * new uploads when backlogged to prevent frame stalls.
     *
     * @param threshold Max pending uploads before considered backlogged (default: 20)
     * @return True if pending uploads >= threshold
     */
    bool isUploadBacklogged(size_t threshold = 20) const;

    /**
     * @brief Get recommended number of chunks to upload this frame
     *
     * Returns 0 if GPU is severely backlogged, otherwise returns
     * a value based on current backlog level for smooth streaming.
     *
     * @return Recommended upload count (0-4)
     */
    size_t getRecommendedUploadCount() const;

    // ========== Indirect Drawing API (GPU Optimization) ==========

    /**
     * @brief Initialize mega-buffers for indirect drawing
     *
     * Creates large device-local buffers to hold all chunk geometry.
     * Call once during initialization.
     */
    void initializeMegaBuffers();

    /**
     * @brief Allocate space in mega-buffer for a chunk
     *
     * Returns offsets where chunk data should be written.
     * Thread-safe for concurrent chunk loading.
     *
     * @param vertexSize Size of vertex data in bytes
     * @param indexSize Size of index data in bytes
     * @param transparent Whether this is for transparent geometry
     * @param outVertexOffset Output: offset in vertex mega-buffer
     * @param outIndexOffset Output: offset in index mega-buffer
     * @return True if allocation succeeded, false if mega-buffer is full
     */
    bool allocateMegaBufferSpace(VkDeviceSize vertexSize, VkDeviceSize indexSize,
                                  bool transparent,
                                  VkDeviceSize& outVertexOffset, VkDeviceSize& outIndexOffset);

    /**
     * @brief Upload chunk geometry to mega-buffer
     *
     * Copies vertex and index data to the specified offsets in mega-buffers.
     *
     * @param vertexData Pointer to vertex data
     * @param vertexSize Size of vertex data in bytes
     * @param indexData Pointer to index data
     * @param indexSize Size of index data in bytes
     * @param vertexOffset Target offset in vertex mega-buffer
     * @param indexOffset Target offset in index mega-buffer
     * @param transparent Whether this is transparent geometry
     */
    void uploadToMegaBuffer(const void* vertexData, VkDeviceSize vertexSize,
                           const void* indexData, VkDeviceSize indexSize,
                           VkDeviceSize vertexOffset, VkDeviceSize indexOffset,
                           bool transparent);

    /**
     * @brief Batched upload to mega-buffer (integrates with batch copy system)
     *
     * Records buffer copies to mega-buffer in current batch command buffer.
     * Use with beginBufferCopyBatch() / submitBufferCopyBatch().
     *
     * @param srcVertexBuffer Source vertex staging buffer
     * @param srcIndexBuffer Source index staging buffer
     * @param vertexSize Size of vertex data
     * @param indexSize Size of index data
     * @param vertexOffset Target offset in mega vertex buffer
     * @param indexOffset Target offset in mega index buffer
     * @param transparent Whether this is transparent geometry
     */
    void batchCopyToMegaBuffer(VkBuffer srcVertexBuffer, VkBuffer srcIndexBuffer,
                               VkDeviceSize vertexSize, VkDeviceSize indexSize,
                               VkDeviceSize vertexOffset, VkDeviceSize indexOffset,
                               bool transparent);

    /**
     * @brief Bind pipeline with state caching (avoids redundant binds)
     *
     * Only calls vkCmdBindPipeline if the pipeline differs from currently bound.
     *
     * @param commandBuffer Command buffer
     * @param pipeline Pipeline to bind
     */
    void bindPipelineCached(VkCommandBuffer commandBuffer, VkPipeline pipeline);

    /**
     * @brief Reset pipeline state cache (call at frame start)
     */
    void resetPipelineCache();

    // ========== Mesh Rendering API ==========

    /**
     * @brief Upload mesh vertex and index data to GPU
     *
     * Creates device-local buffers and uploads mesh geometry using staging buffers.
     *
     * @param vertices Pointer to vertex data
     * @param vertexCount Number of vertices
     * @param indices Pointer to index data
     * @param indexCount Number of indices
     * @param outVertexBuffer Output vertex buffer handle
     * @param outIndexBuffer Output index buffer handle
     * @param outVertexMemory Output vertex buffer memory
     * @param outIndexMemory Output index buffer memory
     */
    void uploadMeshBuffers(const void* vertices, uint32_t vertexCount, uint32_t vertexSize,
                          const void* indices, uint32_t indexCount,
                          VkBuffer& outVertexBuffer, VkBuffer& outIndexBuffer,
                          VkDeviceMemory& outVertexMemory, VkDeviceMemory& outIndexMemory);

    /**
     * @brief Destroy mesh buffers and free memory
     *
     * @param vertexBuffer Vertex buffer to destroy
     * @param indexBuffer Index buffer to destroy
     * @param vertexMemory Vertex memory to free
     * @param indexMemory Index memory to free
     */
    void destroyMeshBuffers(VkBuffer vertexBuffer, VkBuffer indexBuffer,
                           VkDeviceMemory vertexMemory, VkDeviceMemory indexMemory);

    /**
     * @brief Create material uniform buffer
     *
     * @param materialData Pointer to MaterialUBO data
     * @param outBuffer Output buffer handle
     * @param outMemory Output memory handle
     * @param outMapped Output mapped pointer for updates
     */
    void createMaterialBuffer(const void* materialData,
                             VkBuffer& outBuffer,
                             VkDeviceMemory& outMemory,
                             void*& outMapped);

    /**
     * @brief Update material uniform buffer
     *
     * @param mapped Mapped memory pointer
     * @param materialData New material data
     */
    void updateMaterialBuffer(void* mapped, const void* materialData);

    /**
     * @brief Destroy material buffer
     *
     * @param buffer Buffer to destroy
     * @param memory Memory to free
     */
    void destroyMaterialBuffer(VkBuffer buffer, VkDeviceMemory memory);

    /**
     * @brief Reset mega-buffer allocation offsets (clears all chunk data)
     *
     * Call this when mega-buffer is full to reclaim space.
     * WARNING: All chunks must re-upload their data after this!
     */
    void resetMegaBuffers();

    // ========== Mega-Buffer Getters (for Indirect Drawing) ==========
    VkBuffer getMegaVertexBuffer() const { return m_megaVertexBuffer; }
    VkBuffer getMegaIndexBuffer() const { return m_megaIndexBuffer; }
    VkBuffer getMegaTransparentVertexBuffer() const { return m_megaTransparentVertexBuffer; }
    VkBuffer getMegaTransparentIndexBuffer() const { return m_megaTransparentIndexBuffer; }
    VkBuffer getIndirectDrawBuffer() const { return m_indirectDrawBuffer; }
    VkDeviceMemory getIndirectDrawBufferMemory() const { return m_indirectDrawBufferMemory; }
    VkBuffer getIndirectDrawTransparentBuffer() const { return m_indirectDrawTransparentBuffer; }
    VkDeviceMemory getIndirectDrawTransparentBufferMemory() const { return m_indirectDrawTransparentBufferMemory; }

    // ========== Deferred Deletion (Fence-Based Resource Cleanup) ==========

    /**
     * @brief Queue a buffer for deferred deletion
     *
     * Instead of destroying immediately (which requires vkDeviceWaitIdle),
     * this queues the buffer to be destroyed after MAX_FRAMES_IN_FLIGHT frames.
     * This ensures the GPU is no longer using the resource.
     *
     * @param buffer Vulkan buffer to destroy
     * @param memory Device memory to free
     */
    void queueBufferDeletion(VkBuffer buffer, VkDeviceMemory memory);

    /**
     * @brief Flush the deletion queue, destroying old resources
     *
     * Call once per frame (in endFrame). Destroys resources queued
     * at least MAX_FRAMES_IN_FLIGHT frames ago.
     */
    void flushDeletionQueue();

    /**
     * @brief Get the current frame number
     *
     * Used by chunks to tag resources for deferred deletion.
     *
     * @return Total number of frames rendered since app start
     */
    uint64_t getFrameNumber() const { return m_frameNumber; }

    // ========== Command Buffer Utilities ==========

    /**
     * @brief Begins a single-time command buffer for immediate execution
     *
     * Useful for one-off operations like buffer copies or layout transitions.
     *
     * @return Command buffer ready for recording
     */
    VkCommandBuffer beginSingleTimeCommands();

    /**
     * @brief Ends and submits a single-time command buffer
     *
     * Waits for execution to complete before returning.
     *
     * @param commandBuffer Command buffer to end and submit
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    // ========== Memory Utilities ==========

    /**
     * @brief Finds a suitable memory type for allocation
     *
     * @param typeFilter Bitmask of suitable memory types
     * @param properties Required memory properties
     * @return Index of suitable memory type
     * @throws std::runtime_error if no suitable memory type found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // ========== Image/Texture Utilities ==========
    // Public for block system texture loading

    /**
     * @brief Creates a Vulkan image
     *
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param format Image format (e.g., VK_FORMAT_R8G8B8A8_SRGB)
     * @param tiling Tiling mode (linear or optimal)
     * @param usage Image usage flags
     * @param properties Memory properties
     * @param image Output image handle
     * @param imageMemory Output memory handle
     */
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);

    /**
     * @brief Creates an image view for an image
     *
     * @param image Source image
     * @param format Image format
     * @param aspectFlags Aspect mask (e.g., color, depth)
     * @return Image view handle
     */
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    /**
     * @brief Transitions an image to a new layout
     *
     * @param image Image to transition
     * @param format Image format
     * @param oldLayout Current layout
     * @param newLayout Desired layout
     */
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

    /**
     * @brief Copies buffer data to an image
     *
     * @param buffer Source buffer containing pixel data
     * @param image Destination image
     * @param width Image width
     * @param height Image height
     */
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    /**
     * @brief Creates a texture sampler with default settings
     *
     * @return Sampler handle
     */
    VkSampler createTextureSampler();

    /**
     * @brief Creates a default 1x1 white texture
     *
     * Used as fallback for blocks without custom textures.
     */
    void createDefaultTexture();

    // ========== Cube Map Utilities ==========

    /**
     * @brief Creates a cube map image
     *
     * @param width Cube face width
     * @param height Cube face height
     * @param format Image format
     * @param tiling Tiling mode
     * @param usage Usage flags
     * @param properties Memory properties
     * @param image Output cube map image
     * @param imageMemory Output memory handle
     */
    void createCubeMap(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                       VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);

    /**
     * @brief Creates an image view for a cube map
     *
     * @param image Cube map image
     * @param format Image format
     * @return Cube map view
     */
    VkImageView createCubeMapView(VkImage image, VkFormat format);

    /**
     * @brief Transitions all 6 faces of a cube map layout
     *
     * @param image Cube map image
     * @param format Image format
     * @param oldLayout Current layout
     * @param newLayout Desired layout
     */
    void transitionCubeMapLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

    /**
     * @brief Batch transition multiple images in a single command buffer (OPTIMIZATION)
     *
     * Reduces command buffer submissions and synchronization overhead by batching
     * multiple pipeline barriers into a single vkCmdPipelineBarrier call.
     *
     * @param images Vector of images to transition
     * @param formats Vector of image formats (must match images size)
     * @param layerCounts Vector of layer counts (1 for regular textures, 6 for cube maps)
     * @param oldLayout Current layout
     * @param newLayout Desired layout
     */
    void batchTransitionImageLayouts(const std::vector<VkImage>& images,
                                     const std::vector<VkFormat>& formats,
                                     const std::vector<uint32_t>& layerCounts,
                                     VkImageLayout oldLayout,
                                     VkImageLayout newLayout);

private:
    // Initialization
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createTransparentPipeline();
    void createWireframePipeline();
    void createLinePipeline();
    void createSkyboxPipeline();
    void createMeshPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createDepthResources();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();
    void createSkybox();
    void createProceduralCubeMap();
    void createNightCubeMap();

    // Helper functions
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    VkFormat findDepthFormat();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    // Swapchain recreation
    void recreateSwapChain();
    void cleanupSwapChain();

    // Cleanup
    void cleanup();

    // Validation layers
    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#ifdef NDEBUG
    const bool m_enableValidationLayers = false;
#else
    const bool m_enableValidationLayers = true;
#endif

    // Core Vulkan objects
    GLFWwindow* m_window;
    VkInstance m_instance;
    VkDebugUtilsMessengerEXT m_debugMessenger;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    VkQueue m_transferQueue;  ///< PERF (2025-11-24): Dedicated transfer queue for async GPU uploads

    // Queue family indices (needed for queue ownership transfer barriers)
    uint32_t m_graphicsQueueFamily = 0;
    uint32_t m_transferQueueFamily = 0;

    // Swapchain
    VkSwapchainKHR m_swapChain;
    std::vector<VkImage> m_swapChainImages;
    VkFormat m_swapChainImageFormat;
    VkExtent2D m_swapChainExtent;
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkFramebuffer> m_swapChainFramebuffers;

    // Pipeline
    VkRenderPass m_renderPass;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_graphicsPipeline;
    VkPipeline m_wireframePipeline;
    VkPipeline m_linePipeline;
    VkPipeline m_transparentPipeline;  // Same as graphics pipeline but with depth writes disabled
    VkPipeline m_skyboxPipeline;

    // Mesh rendering pipeline (separate from voxel pipeline)
    VkDescriptorSetLayout m_meshDescriptorSetLayout;
    VkPipelineLayout m_meshPipelineLayout;
    VkPipeline m_meshPipeline;

    // Command buffers
    VkCommandPool m_commandPool;
    VkCommandPool m_transferCommandPool;  ///< PERF (2025-11-24): Separate command pool for transfer queue
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Depth buffer
    VkImage m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView m_depthImageView;

    // Uniform buffers
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped;

    // Descriptor pool and sets
    VkDescriptorPool m_descriptorPool;
    std::vector<VkDescriptorSet> m_descriptorSets;

    // Default texture (1x1 white pixel for blocks without custom textures)
    VkImage m_defaultTextureImage;
    VkDeviceMemory m_defaultTextureMemory;
    VkImageView m_defaultTextureView;
    VkSampler m_defaultTextureSampler;

    // Skybox cube map (day)
    VkImage m_skyboxImage;
    VkDeviceMemory m_skyboxMemory;
    VkImageView m_skyboxView;
    VkSampler m_skyboxSampler;

    // Night skybox cube map
    VkImage m_nightSkyboxImage;
    VkDeviceMemory m_nightSkyboxMemory;
    VkImageView m_nightSkyboxView;

    // Skybox geometry (cube vertices)
    VkBuffer m_skyboxVertexBuffer;
    VkDeviceMemory m_skyboxVertexBufferMemory;

    // Sky time for day/night cycle
    float m_skyTime = 0.30f;  // 0.30 = morning (after sunrise)

    // Synchronization
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    // Frame management
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    uint32_t m_imageIndex = 0;
    bool m_framebufferResized = false;
    uint64_t m_frameNumber = 0;  // Total frames rendered (for deferred deletion)

    // Batched buffer copying
    VkCommandBuffer m_batchCommandBuffer = VK_NULL_HANDLE;
    bool m_batchIsAsync = false;  // Track if current batch should be async

    // Multi-chunk batch upload tracking
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> m_batchStagingBuffers;  // Staging buffers for current batch

    // Async upload tracking
    struct PendingUpload {
        VkFence fence;
        VkCommandBuffer commandBuffer;
        std::vector<std::pair<VkBuffer, VkDeviceMemory>> stagingBuffers;
    };
    std::deque<PendingUpload> m_pendingUploads;
    mutable std::mutex m_pendingUploadsMutex;  // mutable for const getters
    static const int MAX_PENDING_UPLOADS = 50;  // Limit concurrent uploads (doubled for reduced stalls)

    // Deferred deletion queue (fence-based resource cleanup)
    struct DeferredDeletion {
        uint64_t frameNumber;
        VkBuffer buffer;
        VkDeviceMemory memory;
    };
    std::deque<DeferredDeletion> m_deletionQueue;
    std::mutex m_deletionQueueMutex;

    // ========== Indirect Drawing System (GPU Optimization) ==========
    // Mega-buffers: Single large buffers containing all chunk geometry
    // This reduces draw calls from 300+ per frame to just 2 (opaque + transparent)

    static constexpr VkDeviceSize MEGA_BUFFER_VERTEX_SIZE = 2ULL * 1024 * 1024 * 1024;  // 2 GB for vertices (terrain changes need more space)
    static constexpr VkDeviceSize MEGA_BUFFER_INDEX_SIZE = 2ULL * 1024 * 1024 * 1024;   // 2 GB for indices (terrain changes need more space)

    // Opaque geometry mega-buffers
    VkBuffer m_megaVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_megaVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_megaIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_megaIndexBufferMemory = VK_NULL_HANDLE;
    VkDeviceSize m_megaVertexOffset = 0;  // Current write offset in vertex mega-buffer
    VkDeviceSize m_megaIndexOffset = 0;   // Current write offset in index mega-buffer

    // Transparent geometry mega-buffers
    VkBuffer m_megaTransparentVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_megaTransparentVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_megaTransparentIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_megaTransparentIndexBufferMemory = VK_NULL_HANDLE;
    VkDeviceSize m_megaTransparentVertexOffset = 0;
    VkDeviceSize m_megaTransparentIndexOffset = 0;

    // Indirect command buffers (rebuilt each frame with visible chunks)
    VkBuffer m_indirectDrawBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indirectDrawBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_indirectDrawTransparentBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indirectDrawTransparentBufferMemory = VK_NULL_HANDLE;

    std::mutex m_megaBufferMutex;  // Protect concurrent chunk uploads

    // Pipeline state caching (reduces redundant vkCmdBindPipeline calls)
    VkPipeline m_currentlyBoundPipeline = VK_NULL_HANDLE;

    // ========== Staging Buffer Pool (2025-11-25) ==========
    // Persistently mapped staging buffers for efficient GPU uploads
    StagingBufferPool m_stagingBufferPool;
    static constexpr VkDeviceSize STAGING_BUFFER_SIZE = 4 * 1024 * 1024;  // 4MB per staging buffer
    static constexpr size_t STAGING_BUFFER_COUNT = 16;  // Pre-allocate 16 buffers (64MB total)

    /**
     * @brief Check if we have a dedicated transfer queue (different from graphics)
     * @return True if transfer and graphics queue families differ
     */
    bool hasDedicatedTransferQueue() const {
        return m_transferQueueFamily != m_graphicsQueueFamily;
    }

private:
    /**
     * @brief Process pending async uploads, cleaning up completed ones
     *
     * Checks fences for pending uploads and cleans up staging buffers
     * for completed uploads. Called automatically in endFrame().
     */
    void processAsyncUploads();
};
