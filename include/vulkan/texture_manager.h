/**
 * @file texture_manager.h
 * @brief Centralized texture and image management for Vulkan
 *
 * Encapsulates all texture-related operations:
 * - Image creation and memory allocation
 * - Image view creation
 * - Texture loading from pixel data
 * - Sampler creation (nearest/linear filtering)
 * - Image layout transitions
 * - Buffer-to-image copy operations
 *
 * Extracted from VulkanRenderer for better separation of concerns.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>

/**
 * @brief Manages Vulkan texture resources and operations
 *
 * Provides a centralized interface for:
 * - Creating and managing VkImage resources
 * - Creating VkImageView for shader access
 * - Creating VkSampler with various filtering modes
 * - Uploading texture data to GPU memory
 * - Transitioning image layouts for different usage patterns
 */
class TextureManager {
public:
    /**
     * @brief Construct a TextureManager
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device (for memory type queries)
     * @param commandPool Command pool for single-time commands
     * @param graphicsQueue Queue for submitting texture operations
     */
    TextureManager(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkCommandPool commandPool, VkQueue graphicsQueue);

    /**
     * @brief Destructor - does not automatically destroy created resources
     *
     * Resources created through this manager must be explicitly destroyed
     * by calling the appropriate destroy methods or Vulkan functions directly.
     */
    ~TextureManager();

    // Prevent copying
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Allow moving
    TextureManager(TextureManager&& other) noexcept;
    TextureManager& operator=(TextureManager&& other) noexcept;

    // ========== Image Creation ==========

    /**
     * @brief Create a 2D image with allocated memory
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param format Pixel format (e.g., VK_FORMAT_R8G8B8A8_SRGB)
     * @param tiling Image tiling mode (optimal for GPU, linear for CPU access)
     * @param usage Usage flags (sampled, transfer dst, etc.)
     * @param properties Memory property flags (device local, host visible, etc.)
     * @param image Output image handle
     * @param imageMemory Output device memory handle
     */
    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);

    /**
     * @brief Create an image view for shader access
     * @param image Source image
     * @param format Image format (must match image creation format)
     * @param aspectFlags Aspect flags (color, depth, stencil)
     * @return Created image view handle
     */
    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspectFlags);

    // ========== Texture Loading ==========

    /**
     * @brief Upload RGBA pixel data to GPU and create texture resources
     * @param pixels RGBA pixel data (4 bytes per pixel)
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param outImage Output image handle
     * @param outMemory Output device memory handle
     *
     * Creates a staging buffer, copies pixel data, creates GPU image,
     * transitions layout, and copies data to GPU memory.
     */
    void uploadTexture(const uint8_t* pixels, uint32_t width, uint32_t height,
                       VkImage& outImage, VkDeviceMemory& outMemory);

    /**
     * @brief Create a 1x1 solid color texture
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     * @param a Alpha component (0-255)
     * @param outImage Output image handle
     * @param outMemory Output device memory handle
     */
    void createSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                  VkImage& outImage, VkDeviceMemory& outMemory);

    // ========== Sampler Creation ==========

    /**
     * @brief Create a texture sampler with configurable filtering
     * @param filter Magnification/minification filter (NEAREST for pixelated, LINEAR for smooth)
     * @param addressMode Address mode for UV wrapping (repeat, clamp, etc.)
     * @return Created sampler handle
     */
    VkSampler createSampler(VkFilter filter = VK_FILTER_NEAREST,
                            VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

    /**
     * @brief Create a nearest-neighbor sampler (pixelated look)
     * @return Created sampler handle
     */
    VkSampler createNearestSampler();

    /**
     * @brief Create a linear filtering sampler with anisotropy
     * @param maxAnisotropy Maximum anisotropy level (1.0 to disable, 4.0-16.0 for quality)
     * @return Created sampler handle
     */
    VkSampler createLinearSampler(float maxAnisotropy = 4.0f);

    // ========== Layout Transitions ==========

    /**
     * @brief Transition image layout for different usage patterns
     * @param image Image to transition
     * @param format Image format (for determining aspect mask)
     * @param oldLayout Current layout
     * @param newLayout Target layout
     *
     * Supported transitions:
     * - UNDEFINED -> TRANSFER_DST_OPTIMAL (for texture upload)
     * - TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL (for shader sampling)
     */
    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout);

    /**
     * @brief Copy buffer data to image
     * @param buffer Source buffer with pixel data
     * @param image Destination image (must be in TRANSFER_DST_OPTIMAL layout)
     * @param width Image width
     * @param height Image height
     */
    void copyBufferToImage(VkBuffer buffer, VkImage image,
                           uint32_t width, uint32_t height);

    // ========== Resource Destruction ==========

    /**
     * @brief Destroy texture resources (image, view, and memory)
     * @param image Image to destroy (set to VK_NULL_HANDLE after destruction)
     * @param imageView Image view to destroy (set to VK_NULL_HANDLE after destruction)
     * @param memory Device memory to free (set to VK_NULL_HANDLE after destruction)
     */
    void destroyTexture(VkImage& image, VkImageView& imageView, VkDeviceMemory& memory);

    /**
     * @brief Destroy a sampler
     * @param sampler Sampler to destroy (set to VK_NULL_HANDLE after destruction)
     */
    void destroySampler(VkSampler& sampler);

private:
    /**
     * @brief Find suitable memory type for allocation
     * @param typeFilter Bitmask of suitable memory types
     * @param properties Required memory properties
     * @return Index of suitable memory type
     * @throws std::runtime_error if no suitable memory type found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /**
     * @brief Begin recording a single-use command buffer
     * @return Allocated and begun command buffer
     */
    VkCommandBuffer beginSingleTimeCommands();

    /**
     * @brief End and submit a single-use command buffer
     * @param commandBuffer Command buffer to submit (freed after completion)
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    /**
     * @brief Create a staging buffer for CPU->GPU transfers
     * @param size Buffer size in bytes
     * @param buffer Output buffer handle
     * @param bufferMemory Output device memory handle
     */
    void createStagingBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkCommandPool m_commandPool;
    VkQueue m_graphicsQueue;
};
