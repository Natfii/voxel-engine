/**
 * @file descriptor_manager.h
 * @brief Vulkan descriptor set, pool, and layout management
 *
 * Encapsulates descriptor-related operations including layout creation,
 * pool management, set allocation, and descriptor updates. Extracted from
 * VulkanRenderer to provide reusable descriptor management functionality.
 *
 * Usage:
 *   DescriptorManager descriptorMgr(device);
 *
 *   // Create a layout with UBO and sampler bindings
 *   std::vector<VkDescriptorSetLayoutBinding> bindings = {...};
 *   VkDescriptorSetLayout layout = descriptorMgr.createLayout(bindings);
 *
 *   // Create a pool
 *   std::vector<VkDescriptorPoolSize> poolSizes = {...};
 *   descriptorMgr.createPool(maxSets, poolSizes);
 *
 *   // Allocate descriptor sets
 *   auto sets = descriptorMgr.allocateSets(layout, count);
 *
 *   // Update descriptors
 *   descriptorMgr.updateUniformBuffer(set, 0, buffer, size);
 *   descriptorMgr.updateCombinedImageSampler(set, 1, imageView, sampler);
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

/**
 * @brief Manages Vulkan descriptor sets, pools, and layouts
 *
 * Provides utility methods for creating descriptor set layouts, managing
 * descriptor pools, allocating descriptor sets, and updating descriptor
 * bindings for uniform buffers and image samplers.
 */
class DescriptorManager {
public:
    /**
     * @brief Construct a new Descriptor Manager
     * @param device Vulkan logical device
     */
    explicit DescriptorManager(VkDevice device);

    /**
     * @brief Destructor - automatically calls cleanup()
     */
    ~DescriptorManager();

    // Disable copy operations
    DescriptorManager(const DescriptorManager&) = delete;
    DescriptorManager& operator=(const DescriptorManager&) = delete;

    // Enable move operations
    DescriptorManager(DescriptorManager&& other) noexcept;
    DescriptorManager& operator=(DescriptorManager&& other) noexcept;

    /**
     * @brief Create a descriptor set layout
     *
     * Creates a new descriptor set layout from the specified bindings.
     * The layout is tracked internally and destroyed during cleanup.
     *
     * @param bindings Vector of descriptor set layout bindings
     * @return VkDescriptorSetLayout The created layout handle
     * @throws std::runtime_error if layout creation fails
     */
    VkDescriptorSetLayout createLayout(
        const std::vector<VkDescriptorSetLayoutBinding>& bindings);

    /**
     * @brief Create a descriptor pool
     *
     * Creates a descriptor pool with the specified maximum sets and pool sizes.
     * Only one pool can be active at a time; call cleanup() before creating
     * a new pool if one already exists.
     *
     * @param maxSets Maximum number of descriptor sets that can be allocated
     * @param poolSizes Vector of pool sizes specifying descriptor type counts
     * @throws std::runtime_error if pool creation fails
     */
    void createPool(uint32_t maxSets,
                   const std::vector<VkDescriptorPoolSize>& poolSizes);

    /**
     * @brief Allocate descriptor sets from the pool
     *
     * Allocates the specified number of descriptor sets using the given layout.
     * The pool must have been created before calling this method.
     *
     * @param layout Descriptor set layout to use for allocation
     * @param count Number of descriptor sets to allocate
     * @return std::vector<VkDescriptorSet> Vector of allocated descriptor sets
     * @throws std::runtime_error if allocation fails or pool is not created
     */
    std::vector<VkDescriptorSet> allocateSets(
        VkDescriptorSetLayout layout, uint32_t count);

    /**
     * @brief Allocate a single descriptor set from the pool
     *
     * Convenience method for allocating a single descriptor set.
     *
     * @param layout Descriptor set layout to use for allocation
     * @return VkDescriptorSet The allocated descriptor set, or VK_NULL_HANDLE on failure
     */
    VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);

    /**
     * @brief Update a uniform buffer descriptor
     *
     * Updates the specified binding in a descriptor set to point to a uniform buffer.
     *
     * @param set Descriptor set to update
     * @param binding Binding index within the set
     * @param buffer Buffer to bind
     * @param size Size of the buffer data
     * @param offset Offset within the buffer (default: 0)
     */
    void updateUniformBuffer(VkDescriptorSet set, uint32_t binding,
                            VkBuffer buffer, VkDeviceSize size,
                            VkDeviceSize offset = 0);

    /**
     * @brief Update a combined image sampler descriptor
     *
     * Updates the specified binding in a descriptor set to point to an image/sampler pair.
     *
     * @param set Descriptor set to update
     * @param binding Binding index within the set
     * @param imageView Image view to bind
     * @param sampler Sampler to bind
     * @param imageLayout Image layout (default: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
     */
    void updateCombinedImageSampler(VkDescriptorSet set, uint32_t binding,
                                    VkImageView imageView, VkSampler sampler,
                                    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /**
     * @brief Update multiple descriptors in a batch
     *
     * Submits multiple descriptor writes in a single Vulkan call for efficiency.
     *
     * @param writes Vector of descriptor write structures
     */
    void updateDescriptors(const std::vector<VkWriteDescriptorSet>& writes);

    /**
     * @brief Destroy a specific descriptor set layout
     *
     * Destroys the layout and removes it from internal tracking.
     *
     * @param layout Layout to destroy
     */
    void destroyLayout(VkDescriptorSetLayout layout);

    /**
     * @brief Clean up all resources
     *
     * Destroys the descriptor pool and all tracked layouts.
     * Safe to call multiple times.
     */
    void cleanup();

    /**
     * @brief Get the descriptor pool
     * @return VkDescriptorPool The current descriptor pool, or VK_NULL_HANDLE if not created
     */
    VkDescriptorPool getPool() const { return m_pool; }

    /**
     * @brief Check if a pool has been created
     * @return true if a pool exists, false otherwise
     */
    bool hasPool() const { return m_pool != VK_NULL_HANDLE; }

    /**
     * @brief Get all tracked layouts
     * @return const std::vector<VkDescriptorSetLayout>& Reference to the layouts vector
     */
    const std::vector<VkDescriptorSetLayout>& getLayouts() const { return m_layouts; }

private:
    VkDevice m_device;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> m_layouts;
};
