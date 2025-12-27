/**
 * @file vulkan_context.h
 * @brief Vulkan context management for instance, device, and queue setup
 *
 * This class encapsulates the core Vulkan initialization:
 * - Vulkan instance creation with validation layers
 * - Debug messenger setup for development
 * - Physical device selection
 * - Logical device creation
 * - Surface creation for window presentation
 * - Queue family discovery and queue retrieval
 *
 * Extracted from VulkanRenderer to improve modularity.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>

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
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }

    /**
     * @brief Checks if a dedicated transfer queue is available
     * @return True if transfer queue exists and differs from graphics queue
     */
    bool hasDedicatedTransferQueue() const {
        return transferFamily.has_value() &&
               graphicsFamily.has_value() &&
               transferFamily.value() != graphicsFamily.value();
    }
};

/**
 * @brief Vulkan context encapsulating instance, device, and queue setup
 *
 * This class manages the core Vulkan objects required for rendering:
 * - VkInstance: The Vulkan instance
 * - VkDebugUtilsMessengerEXT: Debug validation messenger (debug builds)
 * - VkPhysicalDevice: The selected GPU
 * - VkDevice: The logical device for Vulkan operations
 * - VkSurfaceKHR: The window surface for presentation
 * - VkQueue: Graphics, presentation, and transfer queues
 */
class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // Non-copyable, non-movable
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    /**
     * @brief Initialize the Vulkan context
     * @param window GLFW window for surface creation
     *
     * Creates instance, debug messenger, surface, picks physical device,
     * and creates logical device with queues.
     */
    void initialize(GLFWwindow* window);

    /**
     * @brief Cleanup all Vulkan resources
     *
     * Destroys device, surface, debug messenger, and instance in reverse order.
     */
    void cleanup();

    // ========== Getters ==========

    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VkSurfaceKHR getSurface() const { return m_surface; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getPresentQueue() const { return m_presentQueue; }
    VkQueue getTransferQueue() const { return m_transferQueue; }
    QueueFamilyIndices getQueueFamilies() const { return m_queueFamilies; }
    uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    uint32_t getTransferQueueFamily() const { return m_transferQueueFamily; }

    /**
     * @brief Check if validation layers are enabled
     * @return True in debug builds, false in release
     */
    bool isValidationEnabled() const { return m_enableValidationLayers; }

    /**
     * @brief Get the required device extensions
     * @return Vector of extension names
     */
    const std::vector<const char*>& getDeviceExtensions() const { return m_deviceExtensions; }

    /**
     * @brief Get the validation layer names
     * @return Vector of layer names
     */
    const std::vector<const char*>& getValidationLayers() const { return m_validationLayers; }

    // ========== Static Utilities ==========

    /**
     * @brief Find queue families for a physical device
     * @param device Physical device to query
     * @param surface Surface for present support check
     * @return QueueFamilyIndices with available queue families
     */
    static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    // ========== Initialization Methods ==========

    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    // ========== Helper Methods ==========

    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    // ========== Vulkan Objects ==========

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;

    // Queue family indices for queue ownership transfer barriers
    uint32_t m_graphicsQueueFamily = 0;
    uint32_t m_transferQueueFamily = 0;

    // ========== Configuration ==========

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
};
