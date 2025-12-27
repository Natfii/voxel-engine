/**
 * @file swapchain_manager.h
 * @brief Encapsulates Vulkan swapchain creation and management
 *
 * Extracts swapchain logic from VulkanRenderer into a reusable manager class.
 * Handles swapchain creation, recreation on resize, and cleanup.
 *
 * Usage:
 *   SwapchainManager swapchain(device, physicalDevice, surface);
 *   swapchain.create(width, height);
 *   // ... use swapchain.getSwapchain(), getImageViews(), etc.
 *   swapchain.recreate(newWidth, newHeight);
 *   swapchain.cleanup();
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

/**
 * @brief Container for swapchain support query results
 *
 * Query result from Vulkan physical device containing available
 * swapchain configurations.
 */
struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;        ///< Surface capabilities (min/max images, extents)
    std::vector<VkSurfaceFormatKHR> formats;      ///< Supported color formats
    std::vector<VkPresentModeKHR> presentModes;   ///< Supported presentation modes
};

/**
 * @brief Manages Vulkan swapchain lifecycle
 *
 * Encapsulates swapchain creation, image view management, and recreation.
 * Provides getters for swapchain properties needed by render pass and framebuffer creation.
 */
class SwapchainManager {
public:
    /**
     * @brief Construct a new Swapchain Manager
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device
     * @param surface Vulkan surface to present to
     */
    SwapchainManager(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);

    /**
     * @brief Destructor - cleans up swapchain resources
     */
    ~SwapchainManager();

    // Disable copy
    SwapchainManager(const SwapchainManager&) = delete;
    SwapchainManager& operator=(const SwapchainManager&) = delete;

    // Allow move
    SwapchainManager(SwapchainManager&& other) noexcept;
    SwapchainManager& operator=(SwapchainManager&& other) noexcept;

    /**
     * @brief Create the swapchain
     * @param width Desired width in pixels
     * @param height Desired height in pixels
     * @param graphicsFamily Graphics queue family index
     * @param presentFamily Present queue family index
     * @param oldSwapchain Optional old swapchain for recreation (default VK_NULL_HANDLE)
     */
    void create(uint32_t width, uint32_t height,
                uint32_t graphicsFamily, uint32_t presentFamily,
                VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

    /**
     * @brief Cleanup swapchain resources (image views and swapchain)
     *
     * Does not cleanup depth resources, framebuffers, or pipelines -
     * those are managed externally.
     */
    void cleanup();

    /**
     * @brief Recreate swapchain with new dimensions
     * @param width New width in pixels
     * @param height New height in pixels
     * @param graphicsFamily Graphics queue family index
     * @param presentFamily Present queue family index
     *
     * Cleans up old swapchain and creates new one with specified dimensions.
     */
    void recreate(uint32_t width, uint32_t height,
                  uint32_t graphicsFamily, uint32_t presentFamily);

    // ========== Getters ==========

    /**
     * @brief Get the swapchain handle
     * @return VkSwapchainKHR handle
     */
    VkSwapchainKHR getSwapchain() const { return m_swapchain; }

    /**
     * @brief Get the swapchain image format
     * @return VkFormat of swapchain images
     */
    VkFormat getImageFormat() const { return m_imageFormat; }

    /**
     * @brief Get the swapchain extent
     * @return VkExtent2D with width and height
     */
    VkExtent2D getExtent() const { return m_extent; }

    /**
     * @brief Get the swapchain images
     * @return Reference to vector of VkImage handles
     */
    const std::vector<VkImage>& getImages() const { return m_images; }

    /**
     * @brief Get the swapchain image views
     * @return Reference to vector of VkImageView handles
     */
    const std::vector<VkImageView>& getImageViews() const { return m_imageViews; }

    /**
     * @brief Get number of swapchain images
     * @return Image count
     */
    uint32_t getImageCount() const { return static_cast<uint32_t>(m_images.size()); }

    // ========== Static Helpers ==========

    /**
     * @brief Query swapchain support details for a physical device
     * @param device Physical device to query
     * @param surface Surface to check support for
     * @return SwapchainSupportDetails with capabilities, formats, and present modes
     */
    static SwapchainSupportDetails querySupport(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    /**
     * @brief Choose best surface format from available options
     * @param formats Available surface formats
     * @return Selected VkSurfaceFormatKHR
     */
    VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats);

    /**
     * @brief Choose best present mode from available options
     * @param modes Available present modes
     * @return Selected VkPresentModeKHR
     */
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes);

    /**
     * @brief Choose swap extent based on capabilities and desired size
     * @param capabilities Surface capabilities
     * @param width Desired width
     * @param height Desired height
     * @return Selected VkExtent2D
     */
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                            uint32_t width, uint32_t height);

    /**
     * @brief Create image views for swapchain images
     */
    void createImageViews();

    /**
     * @brief Create a single image view
     * @param image Image to create view for
     * @param format Image format
     * @param aspectFlags Aspect mask flags
     * @return Created VkImageView
     */
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    // Vulkan handles (not owned - passed in constructor)
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkSurfaceKHR m_surface;

    // Swapchain resources (owned)
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_imageFormat;
    VkExtent2D m_extent;
    std::vector<VkImage> m_images;        // Images owned by swapchain, not destroyed separately
    std::vector<VkImageView> m_imageViews; // Owned, must be destroyed
};
