/**
 * @file swapchain_manager.cpp
 * @brief Implementation of SwapchainManager class
 *
 * Extracted from VulkanRenderer to encapsulate swapchain management.
 */

#include "vulkan/swapchain_manager.h"
#include <stdexcept>
#include <algorithm>
#include <limits>

// ========== Constructor / Destructor ==========

SwapchainManager::SwapchainManager(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_surface(surface)
    , m_swapchain(VK_NULL_HANDLE)
    , m_imageFormat(VK_FORMAT_UNDEFINED)
    , m_extent{0, 0}
{
}

SwapchainManager::~SwapchainManager() {
    cleanup();
}

SwapchainManager::SwapchainManager(SwapchainManager&& other) noexcept
    : m_device(other.m_device)
    , m_physicalDevice(other.m_physicalDevice)
    , m_surface(other.m_surface)
    , m_swapchain(other.m_swapchain)
    , m_imageFormat(other.m_imageFormat)
    , m_extent(other.m_extent)
    , m_images(std::move(other.m_images))
    , m_imageViews(std::move(other.m_imageViews))
{
    other.m_swapchain = VK_NULL_HANDLE;
    other.m_imageViews.clear();
}

SwapchainManager& SwapchainManager::operator=(SwapchainManager&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = other.m_device;
        m_physicalDevice = other.m_physicalDevice;
        m_surface = other.m_surface;
        m_swapchain = other.m_swapchain;
        m_imageFormat = other.m_imageFormat;
        m_extent = other.m_extent;
        m_images = std::move(other.m_images);
        m_imageViews = std::move(other.m_imageViews);

        other.m_swapchain = VK_NULL_HANDLE;
        other.m_imageViews.clear();
    }
    return *this;
}

// ========== Public Methods ==========

void SwapchainManager::create(uint32_t width, uint32_t height,
                               uint32_t graphicsFamily, uint32_t presentFamily,
                               VkSwapchainKHR oldSwapchain) {
    SwapchainSupportDetails swapchainSupport = querySupport(m_physicalDevice, m_surface);

    VkSurfaceFormatKHR surfaceFormat = chooseFormat(swapchainSupport.formats);
    VkPresentModeKHR presentMode = choosePresentMode(swapchainSupport.presentModes);
    VkExtent2D extent = chooseExtent(swapchainSupport.capabilities, width, height);

    // Request one more than minimum for triple buffering
    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapchainSupport.capabilities.maxImageCount) {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {graphicsFamily, presentFamily};

    if (graphicsFamily != presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain!");
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_images.data());

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;

    // Create image views
    createImageViews();
}

void SwapchainManager::cleanup() {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    // Destroy image views
    for (auto imageView : m_imageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
    }
    m_imageViews.clear();
    m_images.clear();

    // Destroy swapchain
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void SwapchainManager::recreate(uint32_t width, uint32_t height,
                                 uint32_t graphicsFamily, uint32_t presentFamily) {
    VkSwapchainKHR oldSwapchain = m_swapchain;

    // Clear image views but keep old swapchain for recreation
    for (auto imageView : m_imageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
    }
    m_imageViews.clear();
    m_images.clear();

    // Create new swapchain with old one as parameter
    create(width, height, graphicsFamily, presentFamily, oldSwapchain);

    // Destroy old swapchain after new one is created
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);
    }
}

// ========== Static Methods ==========

SwapchainSupportDetails SwapchainManager::querySupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

// ========== Private Methods ==========

VkSurfaceFormatKHR SwapchainManager::chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    // Prefer SRGB with B8G8R8A8 format
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    // Fallback to first available format
    return formats[0];
}

VkPresentModeKHR SwapchainManager::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    // Prefer mailbox (triple buffering) for lower latency
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    // FIFO is guaranteed to be available
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapchainManager::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                           uint32_t width, uint32_t height) {
    // If currentExtent is not the special value, use it
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    // Otherwise, set extent to requested size clamped to allowed range
    VkExtent2D actualExtent = {width, height};

    actualExtent.width = std::clamp(actualExtent.width,
                                     capabilities.minImageExtent.width,
                                     capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                      capabilities.minImageExtent.height,
                                      capabilities.maxImageExtent.height);

    return actualExtent;
}

void SwapchainManager::createImageViews() {
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        m_imageViews[i] = createImageView(m_images[i], m_imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

VkImageView SwapchainManager::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view!");
    }

    return imageView;
}
