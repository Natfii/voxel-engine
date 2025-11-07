/**
 * @file vulkan_renderer.h
 * @brief Vulkan rendering backend with modern graphics pipeline
 *
 * Enhanced API documentation by Claude (Anthropic AI Assistant)
 */

#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <array>

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
    alignas(16) glm::vec4 skyTimeData;   ///< Time data (.x=time 0-1, .y=sun, .z=moon, .w=stars)
};

/**
 * @brief Queue family indices for Vulkan device
 *
 * Identifies which queue families support graphics and presentation.
 */
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;  ///< Queue family supporting graphics ops
    std::optional<uint32_t> presentFamily;   ///< Queue family supporting presentation

    /**
     * @brief Checks if all required queue families are available
     * @return True if both graphics and present queues exist
     */
    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
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
     * @brief Updates uniform buffer with current frame data
     *
     * Uploads MVP matrices, camera position, and render distance to GPU.
     *
     * @param currentImage Swapchain image index
     * @param model Model transformation matrix
     * @param view View (camera) matrix
     * @param projection Projection matrix
     * @param cameraPos Camera position in world space
     * @param renderDistance Maximum render distance for fog
     */
    void updateUniformBuffer(uint32_t currentImage, const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos, float renderDistance);

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
    VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }     ///< Get pipeline layout
    VkCommandBuffer getCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }  ///< Get current cmd buffer
    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }  ///< Get descriptor layout
    VkDescriptorSet getCurrentDescriptorSet() const { return m_descriptorSets[m_currentFrame]; }  ///< Get current descriptor
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
    void createWireframePipeline();
    void createLinePipeline();
    void createSkyboxPipeline();
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
    VkPipeline m_skyboxPipeline;

    // Command buffers
    VkCommandPool m_commandPool;
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
    float m_skyTime = 0.25f;  // 0.25 = morning (sunrise)

    // Synchronization
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    // Frame management
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    uint32_t m_imageIndex = 0;
    bool m_framebufferResized = false;
};
