/**
 * @file buffer_manager.h
 * @brief Vulkan buffer creation and management utilities
 *
 * Encapsulates common buffer operations including creation, memory allocation,
 * and data copying. Extracted from VulkanRenderer to provide reusable buffer
 * management functionality.
 *
 * Usage:
 *   BufferManager bufferMgr(device, physicalDevice);
 *
 *   VkBuffer buffer;
 *   VkDeviceMemory memory;
 *   bufferMgr.createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
 *                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
 *                          buffer, memory);
 */

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

/**
 * @brief Manages Vulkan buffer creation and memory allocation
 *
 * Provides utility methods for creating buffers with appropriate memory types,
 * copying data between buffers, and finding suitable memory types for various
 * buffer usage patterns.
 */
class BufferManager {
public:
    /**
     * @brief Construct a new Buffer Manager
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device (for memory type queries)
     */
    BufferManager(VkDevice device, VkPhysicalDevice physicalDevice);

    /**
     * @brief Create a buffer with allocated memory
     *
     * Creates a Vulkan buffer and allocates device memory with the specified
     * properties. The buffer is automatically bound to the allocated memory.
     *
     * @param size Size of the buffer in bytes
     * @param usage Buffer usage flags (e.g., VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
     * @param properties Memory property flags (e.g., VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
     * @param buffer Output buffer handle
     * @param bufferMemory Output device memory handle
     * @throws std::runtime_error if buffer creation or memory allocation fails
     */
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    /**
     * @brief Copy data from one buffer to another
     *
     * Records and submits a buffer copy command. This is a synchronous operation
     * that waits for the copy to complete before returning.
     *
     * @param commandPool Command pool for allocating the copy command buffer
     * @param queue Queue to submit the copy command to
     * @param srcBuffer Source buffer to copy from
     * @param dstBuffer Destination buffer to copy to
     * @param size Number of bytes to copy
     */
    void copyBuffer(VkCommandPool commandPool, VkQueue queue,
                   VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    /**
     * @brief Find a suitable memory type for the given requirements
     *
     * Searches the physical device's memory types to find one that matches
     * both the type filter (from memory requirements) and the desired properties.
     *
     * @param typeFilter Bitmask of acceptable memory type indices
     * @param properties Required memory property flags
     * @return Memory type index
     * @throws std::runtime_error if no suitable memory type is found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
};
