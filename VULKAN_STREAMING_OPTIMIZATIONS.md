# Vulkan Chunk Streaming Optimizations

**Date:** 2025-11-25
**Purpose:** Fix edge lag when streaming chunks

---

## Critical Finding: Transfer Queue Not Used

**The transfer queue is initialized but never used.** All uploads go through the graphics queue, which is the **primary cause of edge lag**.

### Evidence

```cpp
// vulkan_renderer.cpp:247 - Transfer queue IS created
vkGetDeviceQueue(m_device, indices.transferFamily.value(), 0, &m_transferQueue);

// vulkan_renderer.cpp:2334 - But ALL uploads go to graphics queue!
vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);
```

### Impact

- GPU transfers compete directly with rendering commands
- Cannot overlap chunk uploads with frame rendering
- Uploads stall the render pipeline when approaching chunk boundaries

---

## Fix #1: Use Transfer Queue (HIGHEST PRIORITY)

**File:** `src/vulkan_renderer.cpp`
**Function:** `submitBufferCopyBatch()` (line 2334)

### Change

```cpp
// BEFORE (line 2334):
vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);

// AFTER:
vkQueueSubmit(m_transferQueue, 1, &submitInfo, fence);
```

### Required: Queue Ownership Transfer

When using separate transfer queue, buffers need ownership transfer before the graphics queue can use them:

```cpp
// After transfer completes (in transfer command buffer):
VkBufferMemoryBarrier releaseBarrier{};
releaseBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
releaseBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
releaseBarrier.dstAccessMask = 0;
releaseBarrier.srcQueueFamilyIndex = m_transferQueueFamily;
releaseBarrier.dstQueueFamilyIndex = m_graphicsQueueFamily;
releaseBarrier.buffer = buffer;
releaseBarrier.size = VK_WHOLE_SIZE;

vkCmdPipelineBarrier(transferCmdBuffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0, 0, nullptr, 1, &releaseBarrier, 0, nullptr);

// Before first use on graphics queue:
VkBufferMemoryBarrier acquireBarrier = releaseBarrier;
acquireBarrier.srcAccessMask = 0;
acquireBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

vkCmdPipelineBarrier(graphicsCmdBuffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
    0, 0, nullptr, 1, &acquireBarrier, 0, nullptr);
```

---

## Fix #2: Persistent Buffer Mapping

**Current:** Uses `vkMapMemory`/`vkUnmapMemory` for every transfer (driver overhead per call)

### Solution: Map Once at Creation

```cpp
class StagingBufferPool {
    struct StagingBuffer {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void* mappedPtr;  // Persistently mapped - never unmapped
        size_t size;
        bool inUse;
    };

    std::vector<StagingBuffer> m_pool;

public:
    void initialize(VkDevice device, size_t bufferSize, size_t count) {
        for (size_t i = 0; i < count; i++) {
            StagingBuffer sb;
            createBuffer(device, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                sb.buffer, sb.memory);

            // Map ONCE - never unmap
            vkMapMemory(device, sb.memory, 0, bufferSize, 0, &sb.mappedPtr);
            sb.size = bufferSize;
            sb.inUse = false;
            m_pool.push_back(sb);
        }
    }

    StagingBuffer* acquire() {
        for (auto& sb : m_pool) {
            if (!sb.inUse) { sb.inUse = true; return &sb; }
        }
        return nullptr;
    }

    void release(StagingBuffer* sb) { sb->inUse = false; }
};
```

**Usage:** `memcpy(sb->mappedPtr, data, size)` - no map/unmap calls needed.

---

## Fix #3: Ring Buffer for Staging (Optional but Effective)

Instead of per-chunk staging buffers, use a ring buffer:

```cpp
class RingStagingAllocator {
    static constexpr size_t RING_SIZE = 3;  // Triple buffering
    static constexpr size_t BLOCK_SIZE = 16 * 1024 * 1024;  // 16MB per slot

    struct RingSlot {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void* mappedPtr;
        size_t offset;
        VkFence completionFence;
    };

    RingSlot m_slots[RING_SIZE];
    size_t m_currentSlot = 0;

public:
    void* allocate(size_t size, VkBuffer& outBuffer, VkDeviceSize& outOffset) {
        RingSlot& slot = m_slots[m_currentSlot];

        // Wait if slot still in use
        if (slot.completionFence != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &slot.completionFence, VK_TRUE, UINT64_MAX);
            vkResetFences(device, 1, &slot.completionFence);
            slot.offset = 0;
        }

        outBuffer = slot.buffer;
        outOffset = slot.offset;
        void* ptr = (char*)slot.mappedPtr + slot.offset;
        slot.offset += size;
        return ptr;
    }

    void submitAndAdvance(VkFence fence) {
        m_slots[m_currentSlot].completionFence = fence;
        m_currentSlot = (m_currentSlot + 1) % RING_SIZE;
    }
};
```

**Benefits:**
- Zero per-chunk allocation overhead
- GPU and CPU work on different ring slots simultaneously
- ~48MB fixed memory usage

---

## Implementation Priority

| Fix | Impact | Effort | Lines |
|-----|--------|--------|-------|
| Use transfer queue | **CRITICAL** | Low | ~10 |
| Persistent mapping | High | Medium | ~50 |
| Ring buffer | High | Medium | ~100 |

**Recommended order:**
1. **First:** Change to `m_transferQueue` - may solve edge lag alone
2. **Second:** Add persistent staging buffer pool
3. **Third:** Implement ring buffer if still needed

---

*Generated from friend's Vulkan optimization suggestions, 2025-11-25*
