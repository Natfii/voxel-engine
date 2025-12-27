/**
 * @file descriptor_manager.cpp
 * @brief Implementation of Vulkan descriptor management utilities
 */

#include "vulkan/descriptor_manager.h"
#include <stdexcept>
#include <algorithm>
#include <iostream>

DescriptorManager::DescriptorManager(VkDevice device)
    : m_device(device)
    , m_pool(VK_NULL_HANDLE)
{
}

DescriptorManager::~DescriptorManager() {
    cleanup();
}

DescriptorManager::DescriptorManager(DescriptorManager&& other) noexcept
    : m_device(other.m_device)
    , m_pool(other.m_pool)
    , m_layouts(std::move(other.m_layouts))
{
    other.m_device = VK_NULL_HANDLE;
    other.m_pool = VK_NULL_HANDLE;
}

DescriptorManager& DescriptorManager::operator=(DescriptorManager&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_device = other.m_device;
        m_pool = other.m_pool;
        m_layouts = std::move(other.m_layouts);
        other.m_device = VK_NULL_HANDLE;
        other.m_pool = VK_NULL_HANDLE;
    }
    return *this;
}

VkDescriptorSetLayout DescriptorManager::createLayout(
    const std::vector<VkDescriptorSetLayoutBinding>& bindings) {

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }

    m_layouts.push_back(layout);
    return layout;
}

void DescriptorManager::createPool(uint32_t maxSets,
                                   const std::vector<VkDescriptorPoolSize>& poolSizes) {
    // If a pool already exists, warn but don't fail
    if (m_pool != VK_NULL_HANDLE) {
        std::cerr << "Warning: Creating a new descriptor pool while one already exists. "
                  << "Consider calling cleanup() first." << '\n';
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

std::vector<VkDescriptorSet> DescriptorManager::allocateSets(
    VkDescriptorSetLayout layout, uint32_t count) {

    if (m_pool == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot allocate descriptor sets: pool not created!");
    }

    std::vector<VkDescriptorSetLayout> layouts(count, layout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> descriptorSets(count);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    return descriptorSets;
}

VkDescriptorSet DescriptorManager::allocateSet(VkDescriptorSetLayout layout) {
    if (m_pool == VK_NULL_HANDLE) {
        std::cerr << "Cannot allocate descriptor set: pool not created!" << '\n';
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor set!" << '\n';
        return VK_NULL_HANDLE;
    }

    return descriptorSet;
}

void DescriptorManager::updateUniformBuffer(VkDescriptorSet set, uint32_t binding,
                                            VkBuffer buffer, VkDeviceSize size,
                                            VkDeviceSize offset) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = size;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = set;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
}

void DescriptorManager::updateCombinedImageSampler(VkDescriptorSet set, uint32_t binding,
                                                    VkImageView imageView, VkSampler sampler,
                                                    VkImageLayout imageLayout) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = imageLayout;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = set;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
}

void DescriptorManager::updateDescriptors(const std::vector<VkWriteDescriptorSet>& writes) {
    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void DescriptorManager::destroyLayout(VkDescriptorSetLayout layout) {
    if (layout != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        // Remove from tracked layouts
        auto it = std::find(m_layouts.begin(), m_layouts.end(), layout);
        if (it != m_layouts.end()) {
            m_layouts.erase(it);
        }

        vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
    }
}

void DescriptorManager::cleanup() {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    // Destroy the pool (this automatically frees all allocated descriptor sets)
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }

    // Destroy all tracked layouts
    for (auto layout : m_layouts) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
        }
    }
    m_layouts.clear();
}
