/**
 * @file vulkan_renderer.cpp
 * @brief Vulkan rendering backend implementation for the voxel engine
 *
 * This file implements the VulkanRenderer class which manages:
 * - Vulkan instance, device, and swapchain initialization
 * - Graphics pipeline creation (vertex/fragment shaders, depth testing)
 * - Command buffer recording and submission
 * - Frame synchronization (semaphores, fences)
 * - Buffer and image resource management
 * - Texture atlas loading and mipmapping
 * - Cube-mapped textures for voxel faces
 * - Framebuffer resizing and recreation
 *
 * Created by original author
 */

#include "vulkan_renderer.h"
#include "vulkan/buffer_manager.h"
#include "vulkan/descriptor_manager.h"
#include "vulkan/pipeline_builder.h"
#include "vulkan/skybox_renderer.h"
#include "vulkan/swapchain_manager.h"
#include "vulkan/texture_manager.h"
#include "vulkan/vulkan_context.h"
#include "chunk.h"
#include "mesh/mesh.h"
#include "logger.h"
#include "block_system.h"
#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <chrono>
#include <array>

// ========== StagingBufferPool Implementation (2025-11-25) ==========

void StagingBufferPool::initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize bufferSize, size_t count) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    // Helper to find memory type
    auto findMemoryType = [physicalDevice](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("failed to find suitable memory type for staging buffer pool!");
    };

    for (size_t i = 0; i < count; i++) {
        StagingBuffer sb;
        sb.size = bufferSize;
        sb.inUse = false;

        // Create buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &sb.buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create staging buffer in pool!");
        }

        // Get memory requirements
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, sb.buffer, &memRequirements);

        // Allocate memory
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &sb.memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate staging buffer memory in pool!");
        }

        vkBindBufferMemory(device, sb.buffer, sb.memory, 0);

        // Map memory ONCE - never unmap (persistent mapping)
        vkMapMemory(device, sb.memory, 0, bufferSize, 0, &sb.mappedPtr);

        m_pool.push_back(sb);
    }

    std::cout << "[StagingBufferPool] Initialized " << count << " buffers ("
              << (bufferSize * count / (1024 * 1024)) << " MB total)" << '\n';
}

void StagingBufferPool::cleanup(VkDevice device) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    for (auto& sb : m_pool) {
        if (sb.mappedPtr != nullptr) {
            vkUnmapMemory(device, sb.memory);
        }
        vkDestroyBuffer(device, sb.buffer, nullptr);
        vkFreeMemory(device, sb.memory, nullptr);
    }
    m_pool.clear();
}

StagingBuffer* StagingBufferPool::acquire() {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    for (auto& sb : m_pool) {
        if (!sb.inUse) {
            sb.inUse = true;
            return &sb;
        }
    }
    return nullptr;  // All buffers in use
}

StagingBuffer* StagingBufferPool::acquireWithSize(VkDeviceSize minSize) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    for (auto& sb : m_pool) {
        if (!sb.inUse && sb.size >= minSize) {
            sb.inUse = true;
            return &sb;
        }
    }
    return nullptr;  // No suitable buffer available
}

void StagingBufferPool::release(StagingBuffer* buffer) {
    if (buffer) {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        buffer->inUse = false;
    }
}

size_t StagingBufferPool::getInUseCount() const {
    // Note: Not locking for quick reads (acceptable race)
    size_t count = 0;
    for (const auto& sb : m_pool) {
        if (sb.inUse) count++;
    }
    return count;
}

// ========== End StagingBufferPool Implementation ==========

// Debug callback
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << '\n';
    return VK_FALSE;
}

// Helper to create debug messenger
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

// Helper to read files
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

// Note: CompressedVertex::getBindingDescription() and CompressedVertex::getAttributeDescriptions()
// are defined inline in chunk.h

// Constructor
VulkanRenderer::VulkanRenderer(GLFWwindow* window) : m_window(window) {
    // ========== VulkanContext Integration (2025-12-27) ==========
    // Use VulkanContext for core Vulkan initialization instead of duplicating code
    m_vulkanContext = std::make_unique<VulkanContext>();
    m_vulkanContext->initialize(window);

    // Assign core Vulkan objects from VulkanContext to local member variables
    // This maintains backward compatibility with existing code that uses these members
    m_instance = m_vulkanContext->getInstance();
    m_debugMessenger = VK_NULL_HANDLE;  // Managed by VulkanContext
    m_surface = m_vulkanContext->getSurface();
    m_physicalDevice = m_vulkanContext->getPhysicalDevice();
    m_device = m_vulkanContext->getDevice();
    m_graphicsQueue = m_vulkanContext->getGraphicsQueue();
    m_presentQueue = m_vulkanContext->getPresentQueue();
    m_transferQueue = m_vulkanContext->getTransferQueue();
    m_graphicsQueueFamily = m_vulkanContext->getGraphicsQueueFamily();
    m_transferQueueFamily = m_vulkanContext->getTransferQueueFamily();
    // Note: presentQueueFamily is obtained from QueueFamilyIndices if needed
    QueueFamilyIndices indices = m_vulkanContext->getQueueFamilies();
    m_presentQueueFamily = indices.presentFamily.value();

    // Initialize BufferManager after device creation (2025-12-27)
    m_bufferManager = std::make_unique<BufferManager>(m_device, m_physicalDevice);

    // Initialize DescriptorManager after device creation (2025-12-27)
    m_descriptorManager = std::make_unique<DescriptorManager>(m_device);

    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createTransparentPipeline();
    createWireframePipeline();
    createLinePipeline();
    createMeshPipeline();
    createSpherePipeline();
    createCommandPool();

    // Initialize TextureManager after command pool creation
    m_textureManager = std::make_unique<TextureManager>(m_device, m_physicalDevice, m_commandPool, m_graphicsQueue);

    // Initialize SkyboxRenderer after command pool creation (2025-12-27)
    m_skyboxRenderer = std::make_unique<SkyboxRenderer>(m_device, m_physicalDevice, m_commandPool, m_graphicsQueue);
    m_skyboxRenderer->initialize(m_renderPass, m_pipelineLayout, m_swapChainExtent);

    createDepthResources();
    createFramebuffers();
    createDefaultTexture();  // Create default white texture before descriptor sets
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
    initializeMegaBuffers();  // GPU optimization: indirect drawing mega-buffers

    // PERF (2025-11-25): Initialize staging buffer pool for efficient GPU uploads
    m_stagingBufferPool.initialize(m_device, m_physicalDevice, STAGING_BUFFER_SIZE, STAGING_BUFFER_COUNT);
}

// Destructor
VulkanRenderer::~VulkanRenderer() {
    std::cout << "  Destroying VulkanRenderer..." << '\n';
    std::cout.flush();
    cleanup();
    std::cout << "  VulkanRenderer destroyed" << '\n';
    std::cout.flush();
}


// Create swapchain - delegates to SwapchainManager
void VulkanRenderer::createSwapChain() {
    // Get framebuffer size for swapchain extent
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    // Create SwapchainManager if not already created
    if (!m_swapchainManager) {
        m_swapchainManager = std::make_unique<SwapchainManager>(m_device, m_physicalDevice, m_surface);
    }

    // Create swapchain via manager
    m_swapchainManager->create(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        m_graphicsQueueFamily,
        m_presentQueueFamily
    );

    // Populate legacy member variables for backward compatibility
    m_swapChain = m_swapchainManager->getSwapchain();
    m_swapChainImageFormat = m_swapchainManager->getImageFormat();
    m_swapChainExtent = m_swapchainManager->getExtent();

    // Copy images and image views from manager
    const auto& images = m_swapchainManager->getImages();
    m_swapChainImages.assign(images.begin(), images.end());

    const auto& imageViews = m_swapchainManager->getImageViews();
    m_swapChainImageViews.assign(imageViews.begin(), imageViews.end());
}

// Legacy createSwapChain implementation - kept for reference
#if 0
void VulkanRenderer::createSwapChain_Legacy() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
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

    QueueFamilyIndices indices = VulkanContext::findQueueFamilies(m_physicalDevice, m_surface);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;
}
#endif // Legacy createSwapChain

// Create image views - now handled by SwapchainManager
void VulkanRenderer::createImageViews() {
    // Image views are now created by SwapchainManager and copied in createSwapChain()
    // This method is kept for backward compatibility but does nothing if manager is used
    if (m_swapchainManager) {
        return;
    }

    // Legacy fallback (should not be reached)
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        m_swapChainImageViews[i] = createImageView(m_swapChainImages[i], m_swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

// Create render pass
void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

// Create descriptor set layout
void VulkanRenderer::createDescriptorSetLayout() {
    // UBO binding (binding 0)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;  // Also available in fragment for skybox
    uboLayoutBinding.pImmutableSamplers = nullptr;

    // Texture sampler binding (binding 1)
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    // Cube map sampler binding (binding 2) for day skybox
    VkDescriptorSetLayoutBinding cubeMapLayoutBinding{};
    cubeMapLayoutBinding.binding = 2;
    cubeMapLayoutBinding.descriptorCount = 1;
    cubeMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    cubeMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    cubeMapLayoutBinding.pImmutableSamplers = nullptr;

    // Night cube map sampler binding (binding 3) for night skybox
    VkDescriptorSetLayoutBinding nightCubeMapLayoutBinding{};
    nightCubeMapLayoutBinding.binding = 3;
    nightCubeMapLayoutBinding.descriptorCount = 1;
    nightCubeMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    nightCubeMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    nightCubeMapLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboLayoutBinding, samplerLayoutBinding, cubeMapLayoutBinding, nightCubeMapLayoutBinding};

    // Use DescriptorManager to create the layout
    m_descriptorSetLayout = m_descriptorManager->createLayout(bindings);
}

// Create graphics pipeline
void VulkanRenderer::createGraphicsPipeline() {
    // Get vertex input descriptions for CompressedVertex
    auto binding = CompressedVertex::getBindingDescription();
    auto attrs = CompressedVertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrVec(attrs.begin(), attrs.end());

    // Build graphics pipeline using PipelineBuilder
    PipelineBuilder builder(m_device, m_renderPass);
    m_graphicsPipeline = builder
        .setShaders("shaders/vert.spv", "shaders/frag.spv")
        .setVertexInput(binding, attrVec)
        .setViewport(static_cast<float>(m_swapChainExtent.width),
                     static_cast<float>(m_swapChainExtent.height))
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE)
        .setDepthTest(true, VK_COMPARE_OP_LESS)
        .setAlphaBlending()
        .setDescriptorSetLayout(m_descriptorSetLayout)
        .build(&m_pipelineLayout);
    builder.destroyShaderModules();
}

// Create transparent pipeline (same as graphics pipeline but with depth writes disabled)
void VulkanRenderer::createTransparentPipeline() {
    // Get vertex input descriptions for CompressedVertex
    auto binding = CompressedVertex::getBindingDescription();
    auto attrs = CompressedVertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrVec(attrs.begin(), attrs.end());

    // Build transparent pipeline - same as graphics but no depth write
    PipelineBuilder builder(m_device, m_renderPass);
    m_transparentPipeline = builder
        .setShaders("shaders/vert.spv", "shaders/frag.spv")
        .setVertexInput(binding, attrVec)
        .setViewport(static_cast<float>(m_swapChainExtent.width),
                     static_cast<float>(m_swapChainExtent.height))
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE)
        .setDepthTest(false, VK_COMPARE_OP_LESS)  // Depth test on, write off
        .setAlphaBlending()
        .setPipelineLayout(m_pipelineLayout)  // Reuse existing layout
        .build();
    builder.destroyShaderModules();
}

// Create wireframe pipeline (same as graphics pipeline but with wireframe mode)
void VulkanRenderer::createWireframePipeline() {
    // Get vertex input descriptions for CompressedVertex
    auto binding = CompressedVertex::getBindingDescription();
    auto attrs = CompressedVertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrVec(attrs.begin(), attrs.end());

    // Build wireframe pipeline - same as graphics but with line polygon mode
    PipelineBuilder builder(m_device, m_renderPass);
    m_wireframePipeline = builder
        .setShaders("shaders/vert.spv", "shaders/frag.spv")
        .setVertexInput(binding, attrVec)
        .setViewport(static_cast<float>(m_swapChainExtent.width),
                     static_cast<float>(m_swapChainExtent.height))
        .setPolygonMode(VK_POLYGON_MODE_LINE)  // Wireframe!
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE)
        .setDepthTest(true, VK_COMPARE_OP_LESS)
        .setAlphaBlending()
        .setPipelineLayout(m_pipelineLayout)
        .build();
    builder.destroyShaderModules();
}

// Create line rendering pipeline
void VulkanRenderer::createLinePipeline() {
    // Line vertex format: vec3 position + vec3 color (24 bytes)
    VkVertexInputBindingDescription lineBinding{};
    lineBinding.binding = 0;
    lineBinding.stride = 6 * sizeof(float);
    lineBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> lineAttrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},                    // Position
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float)}     // Color
    };

    // Build line pipeline - for debug lines/outlines
    PipelineBuilder builder(m_device, m_renderPass);
    m_linePipeline = builder
        .setShaders("shaders/line_vert.spv", "shaders/line_frag.spv")
        .setVertexInput(lineBinding, lineAttrs)
        .setViewport(static_cast<float>(m_swapChainExtent.width),
                     static_cast<float>(m_swapChainExtent.height))
        .setTopology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
        .setPolygonMode(VK_POLYGON_MODE_LINE)
        .setLineWidth(2.0f)
        .setNoCull()
        .setNoDepthTest()  // Lines always visible on top
        .setAlphaBlending()
        .setPipelineLayout(m_pipelineLayout)
        .build();
    builder.destroyShaderModules();
}


// Create mesh rendering pipeline
void VulkanRenderer::createMeshPipeline() {
    auto vertShaderCode = readFile("shaders/mesh_vert.spv");
    auto fragShaderCode = readFile("shaders/mesh_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input: MeshVertex (binding 0) + InstanceData (binding 1)
    auto meshVertexBinding = MeshVertex::getBindingDescription();
    auto meshVertexAttributes = MeshVertex::getAttributeDescriptions();

    auto instanceBinding = InstanceData::getBindingDescription();
    auto instanceAttributes = InstanceData::getAttributeDescriptions();

    // Combine bindings and attributes
    std::vector<VkVertexInputBindingDescription> bindings = { meshVertexBinding, instanceBinding };
    std::vector<VkVertexInputAttributeDescription> attributes = meshVertexAttributes;
    attributes.insert(attributes.end(), instanceAttributes.begin(), instanceAttributes.end());

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapChainExtent.width;
    viewport.height = (float)m_swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // Back-face culling for meshes
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;       // Enable depth testing
    depthStencil.depthWriteEnable = VK_TRUE;      // Write to depth buffer
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;  // Standard depth test
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;  // No alpha blending for now (Phase 1)

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Create mesh-specific descriptor set layout for textures (set 1)
    VkDescriptorSetLayoutBinding textureSamplerBinding{};
    textureSamplerBinding.binding = 0;
    textureSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureSamplerBinding.descriptorCount = 64;  // Match shader's meshTextures[64]
    textureSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    textureSamplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
    textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    textureLayoutInfo.bindingCount = 1;
    textureLayoutInfo.pBindings = &textureSamplerBinding;

    if (vkCreateDescriptorSetLayout(m_device, &textureLayoutInfo, nullptr, &m_meshDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh texture descriptor set layout!");
    }

    // Create bone descriptor set layout for skeletal animation (set 2)
    VkDescriptorSetLayoutBinding boneUBOBinding{};
    boneUBOBinding.binding = 0;
    boneUBOBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    boneUBOBinding.descriptorCount = 1;
    boneUBOBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    boneUBOBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo boneLayoutInfo{};
    boneLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    boneLayoutInfo.bindingCount = 1;
    boneLayoutInfo.pBindings = &boneUBOBinding;

    if (vkCreateDescriptorSetLayout(m_device, &boneLayoutInfo, nullptr, &m_meshBoneDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh bone descriptor set layout!");
    }

    // Create push constant range for material data
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(int32_t) * 2 + sizeof(float) * 2;  // albedoTexIndex, normalTexIndex, metallic, roughness

    // Create mesh pipeline layout with three descriptor sets + push constants
    // Set 0: Camera UBO (from voxel pipeline, reused)
    // Set 1: Mesh textures
    // Set 2: Bone matrices for skeletal animation
    std::array<VkDescriptorSetLayout, 3> meshSetLayouts = {
        m_descriptorSetLayout,           // Set 0: Camera UBO
        m_meshDescriptorSetLayout,       // Set 1: Mesh textures
        m_meshBoneDescriptorSetLayout    // Set 2: Bone matrices
    };

    VkPipelineLayoutCreateInfo meshLayoutInfo{};
    meshLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    meshLayoutInfo.setLayoutCount = static_cast<uint32_t>(meshSetLayouts.size());
    meshLayoutInfo.pSetLayouts = meshSetLayouts.data();
    meshLayoutInfo.pushConstantRangeCount = 1;
    meshLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &meshLayoutInfo, nullptr, &m_meshPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline layout!");
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = m_meshPipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_meshPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline!");
    }

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

    Logger::info() << "Mesh rendering pipeline created successfully";
}

// Create sphere pipeline (uses old Vertex format for loading screen)
void VulkanRenderer::createSpherePipeline() {
    // Use old Vertex format (48 bytes)
    auto binding = Vertex::getBindingDescription();
    auto attrs = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrVec(attrs.begin(), attrs.end());

    // Build sphere pipeline
    PipelineBuilder builder(m_device, m_renderPass);
    m_spherePipeline = builder
        .setShaders("shaders/sphere_vert.spv", "shaders/sphere_frag.spv")
        .setVertexInput(binding, attrVec)
        .setViewport(static_cast<float>(m_swapChainExtent.width),
                     static_cast<float>(m_swapChainExtent.height))
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .setDepthTest(true, VK_COMPARE_OP_LESS)
        .setNoBlending()
        .setPipelineLayout(m_pipelineLayout)
        .build();
    builder.destroyShaderModules();

    Logger::info() << "Sphere pipeline created successfully";
}

// Create framebuffers
void VulkanRenderer::createFramebuffers() {
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            m_swapChainImageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

// Create command pool
void VulkanRenderer::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = VulkanContext::findQueueFamilies(m_physicalDevice, m_surface);

    // Graphics command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }

    // PERF (2025-11-24): Transfer command pool for async GPU uploads
    // Use TRANSIENT_BIT flag since transfer commands are short-lived (single-use)
    VkCommandPoolCreateInfo transferPoolInfo{};
    transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    transferPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    transferPoolInfo.queueFamilyIndex = queueFamilyIndices.transferFamily.value();

    if (vkCreateCommandPool(m_device, &transferPoolInfo, nullptr, &m_transferCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create transfer command pool!");
    }
}

// Create depth resources
void VulkanRenderer::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();

    createImage(m_swapChainExtent.width, m_swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_depthImage, m_depthImageMemory);

    m_depthImageView = createImageView(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// Create uniform buffers
void VulkanRenderer::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_uniformBuffers[i], m_uniformBuffersMemory[i]);

        vkMapMemory(m_device, m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);
    }
}

// Create descriptor pool
void VulkanRenderer::createDescriptorPool() {
    // Extra sets for custom textures (loading sphere, ImGui, etc.)
    const uint32_t EXTRA_SETS = 10;
    const uint32_t maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT + EXTRA_SETS);

    std::vector<VkDescriptorPoolSize> poolSizes(2);

    // UBO pool size
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT + EXTRA_SETS);

    // Combined image sampler pool size (for texture atlas + day cube map + night cube map + extras)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>((MAX_FRAMES_IN_FLIGHT + EXTRA_SETS) * 3);

    // Use DescriptorManager to create the pool
    m_descriptorManager->createPool(maxSets, poolSizes);
    m_descriptorPool = m_descriptorManager->getPool();
}

// Create descriptor sets
void VulkanRenderer::createDescriptorSets() {
    // Use DescriptorManager to allocate the descriptor sets
    m_descriptorSets = m_descriptorManager->allocateSets(m_descriptorSetLayout, MAX_FRAMES_IN_FLIGHT);

    // Update descriptor sets using DescriptorManager
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Update UBO (binding 0) using DescriptorManager
        m_descriptorManager->updateUniformBuffer(m_descriptorSets[i], 0, m_uniformBuffers[i], sizeof(UniformBufferObject));

        // Update texture sampler (binding 1) using DescriptorManager
        m_descriptorManager->updateCombinedImageSampler(m_descriptorSets[i], 1, m_defaultTextureView, m_defaultTextureSampler);

        // Update day cube map sampler (binding 2) using DescriptorManager
        m_descriptorManager->updateCombinedImageSampler(m_descriptorSets[i], 2,
            m_skyboxRenderer->getDaySkyboxView(), m_skyboxRenderer->getSampler());

        // Update night cube map sampler (binding 3) using DescriptorManager
        m_descriptorManager->updateCombinedImageSampler(m_descriptorSets[i], 3,
            m_skyboxRenderer->getNightSkyboxView(), m_skyboxRenderer->getSampler());
    }

}

// Create a descriptor set for a custom texture (e.g., map preview on loading sphere)
VkDescriptorSet VulkanRenderer::createCustomTextureDescriptorSet(VkImageView imageView, VkSampler sampler) {
    VkDescriptorSet descriptorSet;

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        std::cerr << "Failed to allocate custom texture descriptor set!" << '\n';
        return VK_NULL_HANDLE;
    }

    // UBO descriptor (use current frame's UBO)
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffers[m_currentFrame];
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    // Custom texture descriptor
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    // Cube map descriptors (use SkyboxRenderer accessors)
    VkDescriptorImageInfo cubeMapInfo{};
    cubeMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    cubeMapInfo.imageView = m_skyboxRenderer->getDaySkyboxView();
    cubeMapInfo.sampler = m_skyboxRenderer->getSampler();

    VkDescriptorImageInfo nightCubeMapInfo{};
    nightCubeMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    nightCubeMapInfo.imageView = m_skyboxRenderer->getNightSkyboxView();
    nightCubeMapInfo.sampler = m_skyboxRenderer->getSampler();

    std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

    // Write UBO (binding 0)
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    // Write custom texture (binding 1)
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    // Write day cube map (binding 2)
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &cubeMapInfo;

    // Write night cube map (binding 3)
    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = descriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pImageInfo = &nightCubeMapInfo;

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                          descriptorWrites.data(), 0, nullptr);

    return descriptorSet;
}

// Create command buffers
void VulkanRenderer::createCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

// Create synchronization objects
void VulkanRenderer::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

// Begin frame
bool VulkanRenderer::beginFrame() {
    // CRITICAL FIX (2025-11-23): Wait for frame resources BEFORE acquiring swap chain image
    // This allows GPU to work on previous frame while CPU waits, reducing idle time
    // Old order: acquire image → wait fence (CPU idle while GPU finishes)
    // New order: wait fence → acquire image (GPU works while CPU waits, then both ready)

    // PERFORMANCE FIX (2025-11-24): Async fence wait with timeout
    // Instead of blocking indefinitely, check if fence is ready with short timeout
    // If GPU is busy, skip frame to prevent massive stalls
    auto fenceWaitStart = std::chrono::high_resolution_clock::now();

    // Try non-blocking check first
    VkResult fenceStatus = vkGetFenceStatus(m_device, m_inFlightFences[m_currentFrame]);

    if (fenceStatus == VK_NOT_READY) {
        // GPU still busy - wait with timeout (100ms = allow GPU to finish complex frames)
        // Previous 8ms was too aggressive for large chunk counts (1000+ chunks)
        // 100ms allows frames up to 10fps before giving up, preventing infinite hangs
        constexpr uint64_t FENCE_TIMEOUT_NS = 100'000'000;  // 100 milliseconds

        VkResult waitResult = vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame],
                                              VK_TRUE, FENCE_TIMEOUT_NS);

        if (waitResult == VK_TIMEOUT) {
            // GPU severely overloaded - only skip if taking > 100ms per frame
            static int skipCounter = 0;
            if (++skipCounter % 10 == 0) {  // Log every 10th skip
                std::cerr << "[GPU OVERLOAD] Skipping frame (GPU busy after 100ms timeout)" << '\n';
            }
            return false;  // Skip frame
        }
    }

    // Fence ready - reset and continue
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    auto fenceWaitEnd = std::chrono::high_resolution_clock::now();
    auto fenceWaitMs = std::chrono::duration_cast<std::chrono::milliseconds>(fenceWaitEnd - fenceWaitStart).count();

    // Log slow fence waits (>10ms indicates GPU backlog)
    static int slowFenceCounter = 0;
    if (fenceWaitMs > 10) {
        size_t pendingCount = 0;
        {
            std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);
            pendingCount = m_pendingUploads.size();
        }

        size_t deletionQueueSize = 0;
        {
            std::lock_guard<std::mutex> deletionLock(m_deletionQueueMutex);
            deletionQueueSize = m_deletionQueue.size();
        }

        // ALWAYS log very slow stalls (>100ms) for debugging
        if (fenceWaitMs > 100) {
            std::cerr << "[GPU STALL] " << fenceWaitMs << "ms fence wait"
                     << " | Pending uploads: " << pendingCount
                     << " | Deletion queue: " << deletionQueueSize
                     << '\n';
        } else if (++slowFenceCounter % 10 == 0) {  // Log every 10th slow fence
            std::cerr << "[GPU FENCE STALL] " << fenceWaitMs << "ms wait"
                     << " | Pending async uploads: " << pendingCount
                     << " | Deletion queue: " << deletionQueueSize
                     << '\n';
        }
    }

    // Now acquire an image from the swap chain
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                            m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return false;  // Skip this frame
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    // ========================================================================
    // PERF (2025-11-25): Memory barrier for async transfer synchronization
    // ========================================================================
    // When using dedicated transfer queue, ensure any completed async transfers
    // are visible to the graphics queue before rendering. This barrier ensures
    // transfer writes to mega-buffers are visible for vertex/index reads.
    // Only needed when we have a dedicated transfer queue (concurrent sharing).
    // ========================================================================
    if (hasDedicatedTransferQueue()) {
        VkMemoryBarrier memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;

        vkCmdPipelineBarrier(
            m_commandBuffers[m_currentFrame],
            VK_PIPELINE_STAGE_TRANSFER_BIT,       // Wait for transfers to complete
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,   // Before vertex input reads
            0,                                     // No dependency flags
            1, &memoryBarrier,                    // Memory barrier
            0, nullptr,                           // No buffer barriers
            0, nullptr                            // No image barriers
        );
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapChainFramebuffers[m_imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.529f, 0.808f, 0.922f, 1.0f}};  // Sky blue
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    vkCmdBindDescriptorSets(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    return true;  // Frame started successfully
}

// End frame
void VulkanRenderer::endFrame() {
    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);

    if (vkEndCommandBuffer(m_commandBuffers[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitResult != VK_SUCCESS) {
        // Log the specific error for debugging
        const char* errorStr = "unknown error";
        switch (submitResult) {
            case VK_ERROR_OUT_OF_HOST_MEMORY: errorStr = "out of host memory"; break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: errorStr = "out of device memory"; break;
            case VK_ERROR_DEVICE_LOST: errorStr = "device lost"; break;
            default: break;
        }
        throw std::runtime_error(std::string("failed to submit draw command buffer: ") + errorStr);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {m_swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    // Increment frame counter and flush old resources
    m_frameNumber++;
    flushDeletionQueue();

    // Process async uploads and clean up completed ones
    processAsyncUploads();

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::waitForGPUIdle() {
    // Wait for all GPU work to complete
    // This is used during initialization to ensure all chunk uploads finish
    // before entering the game loop, preventing initial frame stalls
    vkDeviceWaitIdle(m_device);
}

// Update uniform buffer
void VulkanRenderer::updateUniformBuffer(uint32_t currentImage, const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos, float renderDistance, bool underwater,
                                        const glm::vec3& liquidFogColor, float liquidFogStart, float liquidFogEnd,
                                        const glm::vec3& liquidTintColor, float liquidDarkenFactor) {
    UniformBufferObject ubo{};
    ubo.model = model;
    ubo.view = view;
    ubo.projection = projection;
    ubo.cameraPos = glm::vec4(cameraPos, renderDistance);  // Pack camera position and render distance into vec4

    // Calculate sun, moon intensities based on time of day
    // Time: 0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset, 1.0 = midnight
    float sunIntensity = glm::smoothstep(0.2f, 0.3f, m_skyTime) * (1.0f - glm::smoothstep(0.7f, 0.8f, m_skyTime));
    float moonIntensity = 1.0f - glm::smoothstep(0.15f, 0.25f, m_skyTime) + glm::smoothstep(0.75f, 0.85f, m_skyTime);
    moonIntensity = glm::clamp(moonIntensity, 0.0f, 1.0f);
    float underwaterFlag = underwater ? 1.0f : 0.0f;

    ubo.skyTimeData = glm::vec4(m_skyTime, sunIntensity, moonIntensity, underwaterFlag);

    // Pack liquid properties (from YAML or defaults)
    ubo.liquidFogColor = glm::vec4(liquidFogColor, 1.0f);  // RGB + unused alpha
    ubo.liquidFogDist = glm::vec4(liquidFogStart, liquidFogEnd, 0.0f, 0.0f);  // Start, end, unused, unused
    ubo.liquidTint = glm::vec4(liquidTintColor, liquidDarkenFactor);  // RGB tint + darken factor

    // Atlas info for compressed vertex UV reconstruction
    // .x = atlas grid size (number of cells per row/column)
    // .y = same as .x (square atlas)
    // .z = cell size (1.0 / gridSize, for UV normalization)
    // .w = unused
    float atlasGridSize = static_cast<float>(BlockRegistry::instance().getAtlasGridSize());
    ubo.atlasInfo = glm::vec4(atlasGridSize, atlasGridSize, 1.0f / atlasGridSize, 0.0f);

    memcpy(m_uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

// Update descriptor sets to bind the texture atlas
void VulkanRenderer::bindAtlasTexture(VkImageView atlasView, VkSampler atlasSampler) {
    // Update all descriptor sets to use the atlas texture instead of default texture
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = atlasView;
        imageInfo.sampler = atlasSampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 1;  // Texture sampler is at binding 1
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    }
}


bool VulkanRenderer::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

SwapChainSupportDetails VulkanRenderer::querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanRenderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

VkFormat VulkanRenderer::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkFormat VulkanRenderer::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

void VulkanRenderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                                 VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    // Delegate to TextureManager
    m_textureManager->createImage(width, height, format, tiling, usage, properties, image, imageMemory);
}

VkImageView VulkanRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    // Delegate to TextureManager
    return m_textureManager->createImageView(image, format, aspectFlags);
}

void VulkanRenderer::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    // Delegate to TextureManager
    m_textureManager->transitionImageLayout(image, format, oldLayout, newLayout);
}

void VulkanRenderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    // Delegate to TextureManager
    m_textureManager->copyBufferToImage(buffer, image, width, height);
}

VkSampler VulkanRenderer::createTextureSampler() {
    // Delegate to TextureManager - createNearestSampler provides nearest filtering for pixelated look
    return m_textureManager->createNearestSampler();
}

VkSampler VulkanRenderer::createLinearTextureSampler() {
    // Delegate to TextureManager - createLinearSampler provides linear filtering with anisotropy
    return m_textureManager->createLinearSampler(4.0f);
}

void VulkanRenderer::uploadMeshTexture(const uint8_t* pixels, uint32_t width, uint32_t height,
                                        VkImage& outImage, VkDeviceMemory& outMemory) {
    VkDeviceSize imageSize = width * height * 4;  // RGBA

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);

    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create image
    createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               outImage, outMemory);

    // Transition image layout and copy from staging buffer
    transitionImageLayout(outImage, VK_FORMAT_R8G8B8A8_SRGB,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer, outImage, width, height);
    transitionImageLayout(outImage, VK_FORMAT_R8G8B8A8_SRGB,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Clean up staging buffer
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);
}

void VulkanRenderer::destroyMeshTexture(VkImage image, VkImageView imageView, VkDeviceMemory memory) {
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, image, nullptr);
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, memory, nullptr);
    }
}

void VulkanRenderer::createDefaultTexture() {
    // Create 1x1 white texture as fallback for blocks without custom textures
    unsigned char whitePixel[4] = {255, 255, 255, 255}; // RGBA white

    VkDeviceSize imageSize = 4; // 1 pixel * 4 bytes

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);

    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, whitePixel, imageSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create image
    createImage(1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               m_defaultTextureImage, m_defaultTextureMemory);

    // Transition image layout and copy from staging buffer
    transitionImageLayout(m_defaultTextureImage, VK_FORMAT_R8G8B8A8_SRGB,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer, m_defaultTextureImage, 1, 1);
    transitionImageLayout(m_defaultTextureImage, VK_FORMAT_R8G8B8A8_SRGB,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Cleanup staging buffer
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    // Create image view and sampler
    m_defaultTextureView = createImageView(m_defaultTextureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    m_defaultTextureSampler = createTextureSampler();

    std::cout << "Created default 1x1 white texture for fallback rendering" << '\n';
}

// Create cube map image (6 faces)
void VulkanRenderer::createCubeMap(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                                   VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
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

// Create cube map view
VkImageView VulkanRenderer::createCubeMapView(VkImage image, VkFormat format) {
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

// Transition cube map layout (all 6 faces)
void VulkanRenderer::transitionCubeMapLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
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

// ========== OPTIMIZATION (2025-11-23): BATCHED PIPELINE BARRIERS ==========
// Batch multiple image layout transitions into a single command buffer with one vkCmdPipelineBarrier call.
// This reduces command buffer submissions from N to 1, improving initialization performance.
void VulkanRenderer::batchTransitionImageLayouts(const std::vector<VkImage>& images,
                                                 const std::vector<VkFormat>& formats,
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
        throw std::invalid_argument("unsupported layout transition in batch!");
    }

    // Single vkCmdPipelineBarrier call for all images (OPTIMIZATION!)
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                        0, nullptr,
                        0, nullptr,
                        static_cast<uint32_t>(barriers.size()), barriers.data());

    endSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    // Delegate to BufferManager (2025-12-27)
    m_bufferManager->createBuffer(size, usage, properties, buffer, bufferMemory);
}

void VulkanRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    // Delegate to BufferManager (2025-12-27)
    m_bufferManager->copyBuffer(m_commandPool, m_graphicsQueue, srcBuffer, dstBuffer, size);
}

void VulkanRenderer::beginBufferCopyBatch() {
    // ============================================================================
    // TRANSFER QUEUE OPTIMIZATION (2025-11-25)
    // ============================================================================
    // Use dedicated transfer command pool for async uploads.
    // This allows uploads to run on separate GPU queue, parallel with rendering!
    // Falls back to graphics pool if transfer queue not available.
    // ============================================================================

    // Allocate command buffer for batch
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    // Use transfer pool for async, graphics pool for sync
    allocInfo.commandPool = (m_transferCommandPool != VK_NULL_HANDLE) ? m_transferCommandPool : m_commandPool;
    allocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(m_device, &allocInfo, &m_batchCommandBuffer);

    // Begin recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_batchCommandBuffer, &beginInfo);
    m_batchIsAsync = false;  // Default to sync
}

void VulkanRenderer::beginAsyncChunkUpload() {
    beginBufferCopyBatch();
    m_batchIsAsync = true;  // Mark as async
}

void VulkanRenderer::batchCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    // Record copy command into batch command buffer
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(m_batchCommandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
}

void VulkanRenderer::submitBufferCopyBatch(bool async) {
    // Check if async parameter overrides batch flag
    bool isAsync = async || m_batchIsAsync;

    // End command buffer recording
    vkEndCommandBuffer(m_batchCommandBuffer);

    // Submit all batched copies at once
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_batchCommandBuffer;

    // Create fence to track completion
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence;
    vkCreateFence(m_device, &fenceInfo, nullptr, &fence);

    // ============================================================================
    // TRANSFER QUEUE OPTIMIZATION (2025-11-25)
    // ============================================================================
    // Submit async uploads to transfer queue (runs parallel with graphics!)
    // Falls back to graphics queue if dedicated transfer queue not available.
    // This prevents chunk uploads from stalling the render pipeline.
    // ============================================================================
    VkQueue targetQueue = (isAsync && m_transferQueue != VK_NULL_HANDLE)
                         ? m_transferQueue : m_graphicsQueue;
    vkQueueSubmit(targetQueue, 1, &submitInfo, fence);

    if (isAsync) {
        // ASYNC MODE: Don't wait, store for later cleanup
        std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);

        // Wait if we have too many pending uploads (backpressure)
        if (m_pendingUploads.size() >= MAX_PENDING_UPLOADS) {
            // Process pending uploads to free up slots
            lock.~lock_guard();  // Unlock before processing
            processAsyncUploads();
            new (&lock) std::lock_guard<std::mutex>(m_pendingUploadsMutex);  // Re-lock
        }

        // Store pending upload (no staging buffers yet - will be added by submitAsyncChunkUpload)
        m_pendingUploads.push_back({fence, m_batchCommandBuffer, {}});
        m_batchCommandBuffer = VK_NULL_HANDLE;
    } else {
        // SYNC MODE: Wait for completion (original behavior)
        vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

        // Cleanup immediately - use correct pool based on what was allocated
        vkDestroyFence(m_device, fence, nullptr);
        VkCommandPool cleanupPool = (m_transferCommandPool != VK_NULL_HANDLE)
                                   ? m_transferCommandPool : m_commandPool;
        vkFreeCommandBuffers(m_device, cleanupPool, 1, &m_batchCommandBuffer);
        m_batchCommandBuffer = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::submitAsyncChunkUpload(Chunk* chunk) {
    // Submit the batch asynchronously
    submitBufferCopyBatch(true);

    // Add chunk's staging buffers to the most recent pending upload
    std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);
    if (!m_pendingUploads.empty()) {
        auto& upload = m_pendingUploads.back();

        // Collect all staging buffers from the chunk
        if (chunk->m_vertexStagingBuffer != VK_NULL_HANDLE) {
            upload.stagingBuffers.push_back({chunk->m_vertexStagingBuffer, chunk->m_vertexStagingBufferMemory});
        }
        if (chunk->m_indexStagingBuffer != VK_NULL_HANDLE) {
            upload.stagingBuffers.push_back({chunk->m_indexStagingBuffer, chunk->m_indexStagingBufferMemory});
        }
        if (chunk->m_transparentVertexStagingBuffer != VK_NULL_HANDLE) {
            upload.stagingBuffers.push_back({chunk->m_transparentVertexStagingBuffer, chunk->m_transparentVertexStagingBufferMemory});
        }
        if (chunk->m_transparentIndexStagingBuffer != VK_NULL_HANDLE) {
            upload.stagingBuffers.push_back({chunk->m_transparentIndexStagingBuffer, chunk->m_transparentIndexStagingBufferMemory});
        }

        // Clear chunk's staging buffer references (ownership transferred)
        chunk->m_vertexStagingBuffer = VK_NULL_HANDLE;
        chunk->m_vertexStagingBufferMemory = VK_NULL_HANDLE;
        chunk->m_indexStagingBuffer = VK_NULL_HANDLE;
        chunk->m_indexStagingBufferMemory = VK_NULL_HANDLE;
        chunk->m_transparentVertexStagingBuffer = VK_NULL_HANDLE;
        chunk->m_transparentVertexStagingBufferMemory = VK_NULL_HANDLE;
        chunk->m_transparentIndexStagingBuffer = VK_NULL_HANDLE;
        chunk->m_transparentIndexStagingBufferMemory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::beginBatchedChunkUploads() {
    // Start a new batch command buffer (same as beginAsyncChunkUpload)
    beginBufferCopyBatch();
    m_batchIsAsync = true;

    // Clear staging buffer collection for this batch
    m_batchStagingBuffers.clear();
}

void VulkanRenderer::addChunkToBatch(Chunk* chunk) {
    // Record this chunk's upload commands into the batch command buffer
    // The chunk calls createVertexBufferBatched() which uses batchCopyToMegaBuffer()
    chunk->createVertexBufferBatched(this);

    // Collect staging buffers from this chunk (ownership will transfer on submit)
    if (chunk->m_vertexStagingBuffer != VK_NULL_HANDLE) {
        m_batchStagingBuffers.push_back({chunk->m_vertexStagingBuffer, chunk->m_vertexStagingBufferMemory});
    }
    if (chunk->m_indexStagingBuffer != VK_NULL_HANDLE) {
        m_batchStagingBuffers.push_back({chunk->m_indexStagingBuffer, chunk->m_indexStagingBufferMemory});
    }
    if (chunk->m_transparentVertexStagingBuffer != VK_NULL_HANDLE) {
        m_batchStagingBuffers.push_back({chunk->m_transparentVertexStagingBuffer, chunk->m_transparentVertexStagingBufferMemory});
    }
    if (chunk->m_transparentIndexStagingBuffer != VK_NULL_HANDLE) {
        m_batchStagingBuffers.push_back({chunk->m_transparentIndexStagingBuffer, chunk->m_transparentIndexStagingBufferMemory});
    }

    // Clear chunk's staging buffer references (ownership transferred to batch)
    chunk->m_vertexStagingBuffer = VK_NULL_HANDLE;
    chunk->m_vertexStagingBufferMemory = VK_NULL_HANDLE;
    chunk->m_indexStagingBuffer = VK_NULL_HANDLE;
    chunk->m_indexStagingBufferMemory = VK_NULL_HANDLE;
    chunk->m_transparentVertexStagingBuffer = VK_NULL_HANDLE;
    chunk->m_transparentVertexStagingBufferMemory = VK_NULL_HANDLE;
    chunk->m_transparentIndexStagingBuffer = VK_NULL_HANDLE;
    chunk->m_transparentIndexStagingBufferMemory = VK_NULL_HANDLE;
}

void VulkanRenderer::submitBatchedChunkUploads() {
    // Submit all uploads in a single vkQueueSubmit call
    submitBufferCopyBatch(true);

    // Add all collected staging buffers to the pending upload
    std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);
    if (!m_pendingUploads.empty()) {
        auto& upload = m_pendingUploads.back();

        // Transfer all staging buffers from batch to pending upload
        upload.stagingBuffers.insert(
            upload.stagingBuffers.end(),
            m_batchStagingBuffers.begin(),
            m_batchStagingBuffers.end()
        );

        // Clear the batch staging buffers (ownership transferred)
        m_batchStagingBuffers.clear();
    }
}

void VulkanRenderer::processAsyncUploads() {
    std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);

    // CRITICAL FIX (2025-11-23): Limit staging buffer deletions per frame to prevent stalls
    // Each chunk has 4 staging buffers (vertex, index, transparent vertex, transparent index)
    // PERFORMANCE FIX (2025-11-24): Increased to 50 to match MAX_PENDING_UPLOADS
    // With MAX_PENDING_UPLOADS=50, we need to clean up faster to prevent backlog
    // Must clean up AT LEAST as many as we upload to prevent queue backlog
    // Higher limit allows GPU to catch up faster during heavy streaming
    const int MAX_UPLOAD_COMPLETIONS_PER_FRAME = 50;
    int completionsThisFrame = 0;

    // PERFORMANCE FIX (2025-11-24): Limit fence checks to avoid wasting time on unready uploads
    // If queue has 100 pending uploads but only first 5 are ready, don't check all 100 fences
    // Check up to 2× completion limit (60 fences), then stop to avoid syscall overhead
    const int MAX_FENCE_CHECKS = MAX_UPLOAD_COMPLETIONS_PER_FRAME * 2;
    int fenceChecksThisFrame = 0;

    // Check pending uploads for completion
    auto it = m_pendingUploads.begin();
    while (it != m_pendingUploads.end() &&
           completionsThisFrame < MAX_UPLOAD_COMPLETIONS_PER_FRAME &&
           fenceChecksThisFrame < MAX_FENCE_CHECKS) {
        // Check if this upload is complete (non-blocking check)
        VkResult result = vkGetFenceStatus(m_device, it->fence);
        fenceChecksThisFrame++;

        if (result == VK_SUCCESS) {
            // Upload complete - clean up resources
            vkDestroyFence(m_device, it->fence, nullptr);
            // Use transfer pool for async uploads (command was allocated from there)
            VkCommandPool cleanupPool = (m_transferCommandPool != VK_NULL_HANDLE)
                                       ? m_transferCommandPool : m_commandPool;
            vkFreeCommandBuffers(m_device, cleanupPool, 1, &it->commandBuffer);

            // Destroy all staging buffers
            for (auto& stagingBuffer : it->stagingBuffers) {
                vkDestroyBuffer(m_device, stagingBuffer.first, nullptr);
                vkFreeMemory(m_device, stagingBuffer.second, nullptr);
            }

            // Remove from pending list
            it = m_pendingUploads.erase(it);
            completionsThisFrame++;
        } else if (result == VK_NOT_READY) {
            // Still in progress - check next one
            ++it;
        } else {
            // Error checking fence status
            Logger::error() << "Error checking upload fence status";
            ++it;
        }
    }
}

// ========== GPU Backlog Monitoring (2025-11-25) ==========

size_t VulkanRenderer::getPendingUploadCount() const {
    std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);
    return m_pendingUploads.size();
}

bool VulkanRenderer::isUploadBacklogged(size_t threshold) const {
    std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);
    return m_pendingUploads.size() >= threshold;
}

size_t VulkanRenderer::getRecommendedUploadCount() const {
    std::lock_guard<std::mutex> lock(m_pendingUploadsMutex);
    size_t pending = m_pendingUploads.size();

    // ADAPTIVE UPLOAD RATE based on GPU backlog:
    // PERFORMANCE FIX (2025-11-26): Increased rates for faster chunk visibility
    // - Severely backlogged (>30): Upload 1 chunk to make progress
    // - Backlogged (20-30): Upload 2 chunks
    // - Moderate (10-20): Upload 4 chunks
    // - Light (5-10): Upload 6 chunks
    // - Very light (<5): Upload up to 10 chunks for fast streaming
    if (pending >= 30) {
        return 1;  // GPU heavily loaded but still make progress
    } else if (pending >= 20) {
        return 2;  // Backlogged - slow uploads
    } else if (pending >= 10) {
        return 4;  // Moderate load
    } else if (pending >= 5) {
        return 6;  // Light load
    } else {
        return 10; // GPU keeping up - stream fast to eliminate invisible chunks
    }
}

// ========== Deferred Deletion (Fence-Based Resource Cleanup) ==========

void VulkanRenderer::queueBufferDeletion(VkBuffer buffer, VkDeviceMemory memory) {
    // Thread-safe queue operation
    std::lock_guard<std::mutex> lock(m_deletionQueueMutex);

    // Queue for deletion (will be destroyed after MAX_FRAMES_IN_FLIGHT frames)
    m_deletionQueue.push_back({m_frameNumber, buffer, memory});
}

void VulkanRenderer::flushDeletionQueue() {
    // Thread-safe flush operation
    std::lock_guard<std::mutex> lock(m_deletionQueueMutex);

    // ============================================================================
    // ADAPTIVE DELETION RATE (2025-11-25)
    // ============================================================================
    // Problem: Fixed 2 buffers/frame can't keep up with chunk unloading
    // Solution: Adaptive rate based on queue size
    // - Small queue (<50): 2 buffers/frame (gentle)
    // - Medium queue (50-200): 5 buffers/frame
    // - Large queue (>200): 10 buffers/frame (aggressive catch-up)
    // ============================================================================
    size_t queueSize = m_deletionQueue.size();
    int maxDeletionsPerFrame;

    if (queueSize > 200) {
        maxDeletionsPerFrame = 10;  // Large backlog - aggressive cleanup
    } else if (queueSize > 50) {
        maxDeletionsPerFrame = 5;   // Medium backlog
    } else {
        maxDeletionsPerFrame = 2;   // Normal operation
    }

    int deletionsThisFrame = 0;

    // Log queue size periodically for debugging
    static int frameCounter = 0;
    if (++frameCounter % 300 == 0 && !m_deletionQueue.empty()) {  // Every 5 seconds at 60 FPS
        Logger::debug() << "GPU deletion queue: " << queueSize << " buffers pending"
                       << " (processing " << maxDeletionsPerFrame << "/frame)";
    }

    // Delete resources from frames that are at least MAX_FRAMES_IN_FLIGHT old
    // This ensures the GPU is done using them (fence-based approach)
    while (!m_deletionQueue.empty() && deletionsThisFrame < maxDeletionsPerFrame) {
        const auto& deletion = m_deletionQueue.front();

        // Check if enough frames have passed (GPU is done with this resource)
        if (m_frameNumber - deletion.frameNumber >= MAX_FRAMES_IN_FLIGHT) {
            // Safe to destroy - GPU finished rendering frames that used this resource
            if (deletion.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, deletion.buffer, nullptr);
            }
            if (deletion.memory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, deletion.memory, nullptr);
            }

            m_deletionQueue.pop_front();
            deletionsThisFrame++;
        } else {
            // Too recent - GPU might still be using it
            // All subsequent entries are also too new (FIFO queue), so stop
            break;
        }
    }
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
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

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // PERFORMANCE FIX: Use fence instead of vkQueueWaitIdle for better batching
    // This allows multiple buffer uploads to be submitted before waiting
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence;
    vkCreateFence(m_device, &fenceInfo, nullptr, &fence);

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);

    // Wait for this specific operation to complete (still synchronous, but more efficient)
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void VulkanRenderer::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_device);

    // Cleanup resources that depend on swapchain (but not swapchain itself - manager handles that)
    cleanupSwapChain();

    // Use SwapchainManager's recreate for efficient swapchain recreation
    if (m_swapchainManager) {
        m_swapchainManager->recreate(
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            m_graphicsQueueFamily,
            m_presentQueueFamily
        );

        // Update legacy members from manager
        m_swapChain = m_swapchainManager->getSwapchain();
        m_swapChainImageFormat = m_swapchainManager->getImageFormat();
        m_swapChainExtent = m_swapchainManager->getExtent();

        const auto& images = m_swapchainManager->getImages();
        m_swapChainImages.assign(images.begin(), images.end());

        const auto& imageViews = m_swapchainManager->getImageViews();
        m_swapChainImageViews.assign(imageViews.begin(), imageViews.end());
    } else {
        // Fallback to legacy path
        createSwapChain();
        createImageViews();
    }

    createDepthResources();
    createFramebuffers();

    // Recreate pipelines with new viewport/scissor dimensions
    createGraphicsPipeline();
    createTransparentPipeline();
    createWireframePipeline();
    createLinePipeline();
    m_skyboxRenderer->recreate(m_renderPass, m_swapChainExtent);
    createMeshPipeline();
    createSpherePipeline();
}

// ========== Mesh Buffer Management ==========

void VulkanRenderer::uploadMeshBuffers(const void* vertices, uint32_t vertexCount, uint32_t vertexSize,
                                        const void* indices, uint32_t indexCount,
                                        VkBuffer& outVertexBuffer, VkBuffer& outIndexBuffer,
                                        VkDeviceMemory& outVertexMemory, VkDeviceMemory& outIndexMemory) {
    VkDeviceSize vertexBufferSize = vertexCount * vertexSize;
    VkDeviceSize indexBufferSize = indexCount * sizeof(uint32_t);

    // Create vertex staging buffer
    VkBuffer vertexStagingBuffer;
    VkDeviceMemory vertexStagingMemory;

    createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                vertexStagingBuffer, vertexStagingMemory);

    // Copy vertex data to staging buffer
    void* data;
    vkMapMemory(m_device, vertexStagingMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, vertices, vertexBufferSize);
    vkUnmapMemory(m_device, vertexStagingMemory);

    // Create device-local vertex buffer
    createBuffer(vertexBufferSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                outVertexBuffer, outVertexMemory);

    // Copy from staging to device-local
    copyBuffer(vertexStagingBuffer, outVertexBuffer, vertexBufferSize);

    // Cleanup vertex staging buffer
    vkDestroyBuffer(m_device, vertexStagingBuffer, nullptr);
    vkFreeMemory(m_device, vertexStagingMemory, nullptr);

    // Only create index buffer if indices are provided
    if (indices != nullptr && indexCount > 0) {
        VkBuffer indexStagingBuffer;
        VkDeviceMemory indexStagingMemory;

        createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    indexStagingBuffer, indexStagingMemory);

        // Copy index data to staging buffer
        vkMapMemory(m_device, indexStagingMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, indices, indexBufferSize);
        vkUnmapMemory(m_device, indexStagingMemory);

        // Create device-local index buffer
        createBuffer(indexBufferSize,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    outIndexBuffer, outIndexMemory);

        // Copy from staging to device-local
        copyBuffer(indexStagingBuffer, outIndexBuffer, indexBufferSize);

        // Cleanup index staging buffer
        vkDestroyBuffer(m_device, indexStagingBuffer, nullptr);
        vkFreeMemory(m_device, indexStagingMemory, nullptr);
    } else {
        // No indices - set output handles to null
        outIndexBuffer = VK_NULL_HANDLE;
        outIndexMemory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroyMeshBuffers(VkBuffer vertexBuffer, VkBuffer indexBuffer,
                                         VkDeviceMemory vertexMemory, VkDeviceMemory indexMemory) {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, vertexBuffer, nullptr);
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, indexBuffer, nullptr);
    }
    if (vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, vertexMemory, nullptr);
    }
    if (indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, indexMemory, nullptr);
    }
}

void VulkanRenderer::createMaterialBuffer(const void* materialData,
                                           VkBuffer& outBuffer,
                                           VkDeviceMemory& outMemory,
                                           void*& outMapped) {
    VkDeviceSize bufferSize = sizeof(MaterialUBO);

    createBuffer(bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                outBuffer, outMemory);

    vkMapMemory(m_device, outMemory, 0, bufferSize, 0, &outMapped);
    memcpy(outMapped, materialData, bufferSize);
}

void VulkanRenderer::updateMaterialBuffer(void* mapped, const void* materialData) {
    memcpy(mapped, materialData, sizeof(MaterialUBO));
}

void VulkanRenderer::destroyMaterialBuffer(VkBuffer buffer, VkDeviceMemory memory) {
    if (buffer != VK_NULL_HANDLE) {
        vkUnmapMemory(m_device, memory);
        vkDestroyBuffer(m_device, buffer, nullptr);
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, memory, nullptr);
    }
}

void VulkanRenderer::cleanupSwapChain() {
    // Destroy pipelines (they contain viewport/scissor state)
    vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    vkDestroyPipeline(m_device, m_transparentPipeline, nullptr);
    vkDestroyPipeline(m_device, m_wireframePipeline, nullptr);
    vkDestroyPipeline(m_device, m_linePipeline, nullptr);
    vkDestroyPipeline(m_device, m_spherePipeline, nullptr);
    vkDestroyPipeline(m_device, m_meshPipeline, nullptr);

    vkDestroyImageView(m_device, m_depthImageView, nullptr);
    vkDestroyImage(m_device, m_depthImage, nullptr);
    vkFreeMemory(m_device, m_depthImageMemory, nullptr);

    for (auto framebuffer : m_swapChainFramebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }

    // Swapchain and image views are now managed by SwapchainManager
    // Only cleanup legacy resources if not using manager
    if (!m_swapchainManager) {
        for (auto imageView : m_swapChainImageViews) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    }

    // Clear the legacy vectors (they will be repopulated from manager)
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();
}

void VulkanRenderer::setSkyTime(float timeOfDay) {
    m_skyTime = glm::clamp(timeOfDay, 0.0f, 1.0f);
}

// Render the skybox
void VulkanRenderer::renderSkybox() {
    VkCommandBuffer commandBuffer = m_commandBuffers[m_currentFrame];
    VkDescriptorSet currentDescriptorSet = m_descriptorSets[m_currentFrame];
    m_skyboxRenderer->draw(commandBuffer, currentDescriptorSet);
}

void VulkanRenderer::cleanup() {
    std::cout << "    Cleaning up swapchain..." << '\n';
    cleanupSwapChain();

    std::cout << "    Cleaning up textures..." << '\n';
    // Cleanup default texture
    vkDestroySampler(m_device, m_defaultTextureSampler, nullptr);
    vkDestroyImageView(m_device, m_defaultTextureView, nullptr);
    vkDestroyImage(m_device, m_defaultTextureImage, nullptr);
    vkFreeMemory(m_device, m_defaultTextureMemory, nullptr);

    std::cout << "    Cleaning up skybox renderer..." << '\n';
    m_skyboxRenderer.reset();

    std::cout << "    Cleaning up uniform buffers..." << '\n';
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(m_device, m_uniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_uniformBuffersMemory[i], nullptr);
    }

    std::cout << "    Cleaning up descriptor manager..." << '\n';
    // DescriptorManager handles cleanup of pool and layouts it created
    if (m_descriptorManager) {
        m_descriptorManager.reset();
    }
    // Clear the handles since they're now destroyed by DescriptorManager
    m_descriptorPool = VK_NULL_HANDLE;
    m_descriptorSetLayout = VK_NULL_HANDLE;

    std::cout << "    Cleaning up pipeline layout..." << '\n';
    // Pipelines are already destroyed in cleanupSwapChain()
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

    // Destroy mesh-specific resources
    if (m_meshPipelineLayout != VK_NULL_HANDLE && m_meshPipelineLayout != m_pipelineLayout) {
        vkDestroyPipelineLayout(m_device, m_meshPipelineLayout, nullptr);
    }
    if (m_meshDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_meshDescriptorSetLayout, nullptr);
    }
    if (m_meshBoneDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_meshBoneDescriptorSetLayout, nullptr);
    }
    vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    std::cout << "    Cleaning up sync objects..." << '\n';
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    std::cout << "    Cleaning up mega-buffers..." << '\n';
    // Cleanup indirect drawing mega-buffers
    if (m_megaVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_megaVertexBuffer, nullptr);
        vkFreeMemory(m_device, m_megaVertexBufferMemory, nullptr);
    }
    if (m_megaIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_megaIndexBuffer, nullptr);
        vkFreeMemory(m_device, m_megaIndexBufferMemory, nullptr);
    }
    if (m_megaTransparentVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_megaTransparentVertexBuffer, nullptr);
        vkFreeMemory(m_device, m_megaTransparentVertexBufferMemory, nullptr);
    }
    if (m_megaTransparentIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_megaTransparentIndexBuffer, nullptr);
        vkFreeMemory(m_device, m_megaTransparentIndexBufferMemory, nullptr);
    }
    if (m_indirectDrawBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_indirectDrawBuffer, nullptr);
        vkFreeMemory(m_device, m_indirectDrawBufferMemory, nullptr);
    }
    if (m_indirectDrawTransparentBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_indirectDrawTransparentBuffer, nullptr);
        vkFreeMemory(m_device, m_indirectDrawTransparentBufferMemory, nullptr);
    }

    std::cout << "    Waiting for pending async uploads..." << '\n';
    // Wait for all pending uploads and clean them up
    for (auto& upload : m_pendingUploads) {
        vkWaitForFences(m_device, 1, &upload.fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(m_device, upload.fence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &upload.commandBuffer);
        for (auto& stagingBuffer : upload.stagingBuffers) {
            vkDestroyBuffer(m_device, stagingBuffer.first, nullptr);
            vkFreeMemory(m_device, stagingBuffer.second, nullptr);
        }
    }
    m_pendingUploads.clear();

    // PERF (2025-11-25): Cleanup staging buffer pool
    std::cout << "    Cleaning up staging buffer pool..." << '\n';
    m_stagingBufferPool.cleanup(m_device);

    std::cout << "    Cleaning up command pools..." << '\n';
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyCommandPool(m_device, m_transferCommandPool, nullptr);  // PERF (2025-11-24): Transfer command pool

    // Cleanup SwapchainManager before destroying device
    std::cout << "    Cleaning up swapchain manager..." << '\n';
    if (m_swapchainManager) {
        m_swapchainManager->cleanup();
        m_swapchainManager.reset();
    }

    // ========== VulkanContext Cleanup (2025-12-27) ==========
    // VulkanContext now handles cleanup of: device, debug messenger, surface, instance
    std::cout << "    Cleaning up VulkanContext..." << '\n';
    if (m_vulkanContext) {
        m_vulkanContext->cleanup();
        m_vulkanContext.reset();
    }

    std::cout << "    Vulkan cleanup complete" << '\n';
}

// ========================================================================
// INDIRECT DRAWING SYSTEM (GPU OPTIMIZATION)
// ========================================================================

void VulkanRenderer::initializeMegaBuffers() {
    std::cout << "Initializing mega-buffers for indirect drawing..." << '\n';

    // ========================================================================
    // PERF (2025-11-25): Use VK_SHARING_MODE_CONCURRENT for mega-buffers
    // ========================================================================
    // When using a dedicated transfer queue, mega-buffers are accessed by both:
    //   - Transfer queue: writes data from staging buffers
    //   - Graphics queue: reads for rendering
    // Using concurrent sharing avoids explicit queue ownership transfer barriers.
    // ========================================================================
    const bool useConcurrentSharing = hasDedicatedTransferQueue();
    std::vector<uint32_t> queueFamilyIndices;
    if (useConcurrentSharing) {
        queueFamilyIndices = {m_graphicsQueueFamily, m_transferQueueFamily};
        std::cout << "  Using VK_SHARING_MODE_CONCURRENT (dedicated transfer queue)" << '\n';
    } else {
        std::cout << "  Using VK_SHARING_MODE_EXCLUSIVE (shared queue)" << '\n';
    }

    // Helper lambda to create a mega-buffer with proper sharing mode
    auto createMegaBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;

        if (useConcurrentSharing) {
            bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            bufferInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
            bufferInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        } else {
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create mega-buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate mega-buffer memory!");
        }

        vkBindBufferMemory(m_device, buffer, memory, 0);
    };

    // Create opaque vertex mega-buffer
    createMegaBuffer(
        MEGA_BUFFER_VERTEX_SIZE,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        m_megaVertexBuffer,
        m_megaVertexBufferMemory
    );

    // Create opaque index mega-buffer
    createMegaBuffer(
        MEGA_BUFFER_INDEX_SIZE,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        m_megaIndexBuffer,
        m_megaIndexBufferMemory
    );

    // Create transparent vertex mega-buffer
    createMegaBuffer(
        MEGA_BUFFER_VERTEX_SIZE,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        m_megaTransparentVertexBuffer,
        m_megaTransparentVertexBufferMemory
    );

    // Create transparent index mega-buffer
    createMegaBuffer(
        MEGA_BUFFER_INDEX_SIZE,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        m_megaTransparentIndexBuffer,
        m_megaTransparentIndexBufferMemory
    );

    // Create indirect command buffers (max 4096 chunks per draw)
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndexedIndirectCommand) * 4096;
    
    createBuffer(
        indirectBufferSize,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_indirectDrawBuffer,
        m_indirectDrawBufferMemory
    );

    createBuffer(
        indirectBufferSize,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_indirectDrawTransparentBuffer,
        m_indirectDrawTransparentBufferMemory
    );

    std::cout << "Mega-buffers initialized successfully!" << '\n';
    std::cout << "  Vertex buffer: " << (MEGA_BUFFER_VERTEX_SIZE / 1024 / 1024) << " MB" << '\n';
    std::cout << "  Index buffer: " << (MEGA_BUFFER_INDEX_SIZE / 1024 / 1024) << " MB" << '\n';
}

bool VulkanRenderer::allocateMegaBufferSpace(VkDeviceSize vertexSize, VkDeviceSize indexSize,
                                              bool transparent,
                                              VkDeviceSize& outVertexOffset, VkDeviceSize& outIndexOffset) {
    std::lock_guard<std::mutex> lock(m_megaBufferMutex);

    if (transparent) {
        // Check if there's enough space in transparent mega-buffers
        if (m_megaTransparentVertexOffset + vertexSize > MEGA_BUFFER_VERTEX_SIZE ||
            m_megaTransparentIndexOffset + indexSize > MEGA_BUFFER_INDEX_SIZE) {
            std::cerr << "Warning: Transparent mega-buffer full!" << '\n';
            return false;
        }

        outVertexOffset = m_megaTransparentVertexOffset;
        outIndexOffset = m_megaTransparentIndexOffset;

        m_megaTransparentVertexOffset += vertexSize;
        m_megaTransparentIndexOffset += indexSize;
    } else {
        // Check if there's enough space in opaque mega-buffers
        if (m_megaVertexOffset + vertexSize > MEGA_BUFFER_VERTEX_SIZE ||
            m_megaIndexOffset + indexSize > MEGA_BUFFER_INDEX_SIZE) {
            std::cerr << "Warning: Opaque mega-buffer full!" << '\n';
            return false;
        }

        outVertexOffset = m_megaVertexOffset;
        outIndexOffset = m_megaIndexOffset;

        m_megaVertexOffset += vertexSize;
        m_megaIndexOffset += indexSize;
    }

    return true;
}

void VulkanRenderer::uploadToMegaBuffer(const void* vertexData, VkDeviceSize vertexSize,
                                        const void* indexData, VkDeviceSize indexSize,
                                        VkDeviceSize vertexOffset, VkDeviceSize indexOffset,
                                        bool transparent) {
    // Create staging buffers
    VkBuffer vertexStagingBuffer, indexStagingBuffer;
    VkDeviceMemory vertexStagingMemory, indexStagingMemory;

    // Vertex staging buffer
    createBuffer(
        vertexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertexStagingBuffer,
        vertexStagingMemory
    );

    // Index staging buffer
    createBuffer(
        indexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indexStagingBuffer,
        indexStagingMemory
    );

    // Copy data to staging buffers
    void* data;
    vkMapMemory(m_device, vertexStagingMemory, 0, vertexSize, 0, &data);
    memcpy(data, vertexData, vertexSize);
    vkUnmapMemory(m_device, vertexStagingMemory);

    vkMapMemory(m_device, indexStagingMemory, 0, indexSize, 0, &data);
    memcpy(data, indexData, indexSize);
    vkUnmapMemory(m_device, indexStagingMemory);

    // Copy staging buffers to mega-buffers at specified offsets
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy vertexCopyRegion{};
    vertexCopyRegion.srcOffset = 0;
    vertexCopyRegion.dstOffset = vertexOffset;
    vertexCopyRegion.size = vertexSize;

    VkBufferCopy indexCopyRegion{};
    indexCopyRegion.srcOffset = 0;
    indexCopyRegion.dstOffset = indexOffset;
    indexCopyRegion.size = indexSize;

    vkCmdCopyBuffer(commandBuffer, vertexStagingBuffer,
                   transparent ? m_megaTransparentVertexBuffer : m_megaVertexBuffer,
                   1, &vertexCopyRegion);
    
    vkCmdCopyBuffer(commandBuffer, indexStagingBuffer,
                   transparent ? m_megaTransparentIndexBuffer : m_megaIndexBuffer,
                   1, &indexCopyRegion);

    endSingleTimeCommands(commandBuffer);

    // Cleanup staging buffers
    vkDestroyBuffer(m_device, vertexStagingBuffer, nullptr);
    vkFreeMemory(m_device, vertexStagingMemory, nullptr);
    vkDestroyBuffer(m_device, indexStagingBuffer, nullptr);
    vkFreeMemory(m_device, indexStagingMemory, nullptr);
}

void VulkanRenderer::bindPipelineCached(VkCommandBuffer commandBuffer, VkPipeline pipeline) {
    if (m_currentlyBoundPipeline != pipeline) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        m_currentlyBoundPipeline = pipeline;
    }
}

void VulkanRenderer::resetPipelineCache() {
    m_currentlyBoundPipeline = VK_NULL_HANDLE;
}

void VulkanRenderer::bindMeshDescriptorSets(VkCommandBuffer cmd, VkDescriptorSet textureDescriptorSet,
                                            VkDescriptorSet boneDescriptorSet) {
    // Bind the camera descriptor set (set 0)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipelineLayout,
                            0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    // Bind the texture descriptor set (set 1) if provided
    if (textureDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipelineLayout,
                                1, 1, &textureDescriptorSet, 0, nullptr);
    }

    // Bind the bone descriptor set (set 2) if provided for skeletal animation
    if (boneDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipelineLayout,
                                2, 1, &boneDescriptorSet, 0, nullptr);
    }
}

void VulkanRenderer::pushMeshMaterialConstants(VkCommandBuffer cmd, int32_t albedoTexIndex, int32_t normalTexIndex,
                                                float metallic, float roughness) {
    struct MaterialPushConstant {
        int32_t albedoTexIndex;
        int32_t normalTexIndex;
        float metallic;
        float roughness;
    } pushData;

    pushData.albedoTexIndex = albedoTexIndex;
    pushData.normalTexIndex = normalTexIndex;
    pushData.metallic = metallic;
    pushData.roughness = roughness;

    vkCmdPushConstants(cmd, m_meshPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushData), &pushData);
}

void VulkanRenderer::batchCopyToMegaBuffer(VkBuffer srcVertexBuffer, VkBuffer srcIndexBuffer,
                                           VkDeviceSize vertexSize, VkDeviceSize indexSize,
                                           VkDeviceSize vertexOffset, VkDeviceSize indexOffset,
                                           bool transparent) {
    if (m_batchCommandBuffer == VK_NULL_HANDLE) {
        std::cerr << "Error: No batch command buffer active. Call beginBufferCopyBatch() first." << '\n';
        return;
    }

    // Copy vertex data to mega-buffer at offset
    VkBufferCopy vertexCopyRegion{};
    vertexCopyRegion.srcOffset = 0;
    vertexCopyRegion.dstOffset = vertexOffset;
    vertexCopyRegion.size = vertexSize;

    vkCmdCopyBuffer(m_batchCommandBuffer, srcVertexBuffer,
                   transparent ? m_megaTransparentVertexBuffer : m_megaVertexBuffer,
                   1, &vertexCopyRegion);

    // Copy index data to mega-buffer at offset
    VkBufferCopy indexCopyRegion{};
    indexCopyRegion.srcOffset = 0;
    indexCopyRegion.dstOffset = indexOffset;
    indexCopyRegion.size = indexSize;

    vkCmdCopyBuffer(m_batchCommandBuffer, srcIndexBuffer,
                   transparent ? m_megaTransparentIndexBuffer : m_megaIndexBuffer,
                   1, &indexCopyRegion);
}

void VulkanRenderer::resetMegaBuffers() {
    std::lock_guard<std::mutex> lock(m_megaBufferMutex);
    
    Logger::info() << "Resetting mega-buffers (reclaiming space)...";
    
    // Reset allocation offsets to beginning
    m_megaVertexOffset = 0;
    m_megaIndexOffset = 0;
    m_megaTransparentVertexOffset = 0;
    m_megaTransparentIndexOffset = 0;
    
    Logger::info() << "Mega-buffers reset complete - all space reclaimed";
}
