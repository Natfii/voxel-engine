/**
 * @file pipeline_builder.h
 * @brief Fluent builder pattern for Vulkan graphics pipeline creation
 *
 * Eliminates 1000+ lines of duplicated pipeline creation code by providing
 * a reusable, configurable builder. Based on vkguide.dev patterns.
 *
 * Usage:
 *   auto pipeline = PipelineBuilder(device, renderPass)
 *       .setShaders("vert.spv", "frag.spv")
 *       .setVertexInput(CompressedVertex::getBindingDescription(),
 *                       CompressedVertex::getAttributeDescriptions())
 *       .setDepthWrite(true)
 *       .build(&outPipelineLayout);
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <array>

/**
 * @brief Vertex input configuration types for common vertex formats
 */
enum class VertexInputType {
    None,               ///< No vertex input (fullscreen quad, etc.)
    CompressedVertex,   ///< Chunk rendering (packed 12-byte vertices)
    MeshVertex,         ///< 3D mesh rendering (position, normal, UV, tangent)
    SkyboxVertex,       ///< Skybox rendering (position only)
    LineVertex          ///< Line rendering (position + color)
};

/**
 * @brief Blend mode presets
 */
enum class BlendMode {
    None,           ///< No blending (opaque)
    Alpha,          ///< Standard alpha blending
    Additive,       ///< Additive blending (particles, lights)
    Multiply        ///< Multiplicative blending
};

/**
 * @brief Fluent builder for Vulkan graphics pipelines
 *
 * Provides sensible defaults and chainable methods for configuration.
 * Call build() to create the final pipeline.
 */
class PipelineBuilder {
public:
    /**
     * @brief Construct a new Pipeline Builder
     * @param device Vulkan logical device
     * @param renderPass Render pass the pipeline will be used with
     */
    PipelineBuilder(VkDevice device, VkRenderPass renderPass);

    /**
     * @brief Reset builder to default state for reuse
     * @return Reference to this builder
     */
    PipelineBuilder& clear();

    // ========== Shader Configuration ==========

    /**
     * @brief Set vertex and fragment shaders from SPIR-V files
     * @param vertPath Path to vertex shader .spv file
     * @param fragPath Path to fragment shader .spv file
     * @return Reference to this builder
     */
    PipelineBuilder& setShaders(const std::string& vertPath, const std::string& fragPath);

    /**
     * @brief Set shaders from pre-loaded shader modules
     * @param vertModule Vertex shader module
     * @param fragModule Fragment shader module
     * @return Reference to this builder
     */
    PipelineBuilder& setShaderModules(VkShaderModule vertModule, VkShaderModule fragModule);

    // ========== Vertex Input Configuration ==========

    /**
     * @brief Set vertex input from binding and attribute descriptions
     * @param binding Vertex binding description
     * @param attributes Vertex attribute descriptions
     * @return Reference to this builder
     */
    PipelineBuilder& setVertexInput(
        const VkVertexInputBindingDescription& binding,
        const std::vector<VkVertexInputAttributeDescription>& attributes);

    /**
     * @brief Set vertex input using a predefined type
     * @param type Vertex input type preset
     * @return Reference to this builder
     */
    PipelineBuilder& setVertexInputType(VertexInputType type);

    /**
     * @brief Disable vertex input (for fullscreen quads, etc.)
     * @return Reference to this builder
     */
    PipelineBuilder& setNoVertexInput();

    // ========== Input Assembly ==========

    /**
     * @brief Set primitive topology
     * @param topology Primitive type (triangles, lines, etc.)
     * @return Reference to this builder
     */
    PipelineBuilder& setTopology(VkPrimitiveTopology topology);

    // ========== Rasterization ==========

    /**
     * @brief Set polygon fill mode
     * @param mode Fill mode (fill, line, point)
     * @return Reference to this builder
     */
    PipelineBuilder& setPolygonMode(VkPolygonMode mode);

    /**
     * @brief Set culling mode
     * @param cullMode Cull mode flags
     * @param frontFace Front face winding order
     * @return Reference to this builder
     */
    PipelineBuilder& setCullMode(VkCullModeFlags cullMode,
                                  VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE);

    /**
     * @brief Disable culling
     * @return Reference to this builder
     */
    PipelineBuilder& setNoCull();

    /**
     * @brief Set line width for line primitives
     * @param width Line width in pixels
     * @return Reference to this builder
     */
    PipelineBuilder& setLineWidth(float width);

    // ========== Depth Testing ==========

    /**
     * @brief Enable depth testing with specified settings
     * @param depthWrite Enable depth buffer writes
     * @param compareOp Depth comparison operation
     * @return Reference to this builder
     */
    PipelineBuilder& setDepthTest(bool depthWrite,
                                   VkCompareOp compareOp = VK_COMPARE_OP_LESS);

    /**
     * @brief Disable depth testing entirely
     * @return Reference to this builder
     */
    PipelineBuilder& setNoDepthTest();

    /**
     * @brief Shorthand: enable depth test with writes
     * @return Reference to this builder
     */
    PipelineBuilder& setDepthWrite(bool enabled);

    // ========== Blending ==========

    /**
     * @brief Set blend mode using preset
     * @param mode Blend mode preset
     * @return Reference to this builder
     */
    PipelineBuilder& setBlendMode(BlendMode mode);

    /**
     * @brief Enable standard alpha blending
     * @return Reference to this builder
     */
    PipelineBuilder& setAlphaBlending();

    /**
     * @brief Disable blending (opaque rendering)
     * @return Reference to this builder
     */
    PipelineBuilder& setNoBlending();

    // ========== Multisampling ==========

    /**
     * @brief Set multisampling (MSAA)
     * @param samples Number of samples
     * @return Reference to this builder
     */
    PipelineBuilder& setMultisampling(VkSampleCountFlagBits samples);

    /**
     * @brief Disable multisampling
     * @return Reference to this builder
     */
    PipelineBuilder& setNoMultisampling();

    // ========== Pipeline Layout ==========

    /**
     * @brief Set descriptor set layout for pipeline
     * @param layout Descriptor set layout
     * @return Reference to this builder
     */
    PipelineBuilder& setDescriptorSetLayout(VkDescriptorSetLayout layout);

    /**
     * @brief Set multiple descriptor set layouts
     * @param layouts Vector of descriptor set layouts
     * @return Reference to this builder
     */
    PipelineBuilder& setDescriptorSetLayouts(const std::vector<VkDescriptorSetLayout>& layouts);

    /**
     * @brief Add push constant range
     * @param stageFlags Shader stages that access push constants
     * @param offset Offset in bytes
     * @param size Size in bytes
     * @return Reference to this builder
     */
    PipelineBuilder& addPushConstantRange(VkShaderStageFlags stageFlags,
                                           uint32_t offset, uint32_t size);

    /**
     * @brief Set existing pipeline layout (skips layout creation)
     * @param layout Pre-created pipeline layout
     * @return Reference to this builder
     */
    PipelineBuilder& setPipelineLayout(VkPipelineLayout layout);

    // ========== Dynamic State ==========

    /**
     * @brief Enable dynamic viewport and scissor
     * @return Reference to this builder
     */
    PipelineBuilder& setDynamicViewport();

    /**
     * @brief Set fixed viewport dimensions
     * @param width Viewport width
     * @param height Viewport height
     * @return Reference to this builder
     */
    PipelineBuilder& setViewport(float width, float height);

    // ========== Build ==========

    /**
     * @brief Build the graphics pipeline
     * @param outLayout Optional output for created pipeline layout (if not using existing)
     * @return Created VkPipeline handle
     * @throws std::runtime_error if pipeline creation fails
     */
    VkPipeline build(VkPipelineLayout* outLayout = nullptr);

    /**
     * @brief Destroy shader modules created during setShaders()
     *
     * Call this after build() if you used setShaders() with file paths.
     * Not needed if you used setShaderModules() with pre-loaded modules.
     */
    void destroyShaderModules();

private:
    // Helper functions
    std::vector<char> readShaderFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    // Vulkan handles
    VkDevice m_device;
    VkRenderPass m_renderPass;

    // Shader modules (owned if loaded from files)
    VkShaderModule m_vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule m_fragShaderModule = VK_NULL_HANDLE;
    bool m_ownsShaderModules = false;

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;

    // Vertex input state
    VkVertexInputBindingDescription m_vertexBinding{};
    std::vector<VkVertexInputAttributeDescription> m_vertexAttributes;
    bool m_hasVertexInput = false;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo m_inputAssembly{};

    // Viewport (for fixed viewport, otherwise dynamic)
    VkViewport m_viewport{};
    VkRect2D m_scissor{};
    bool m_useDynamicViewport = false;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo m_rasterizer{};

    // Multisampling
    VkPipelineMultisampleStateCreateInfo m_multisampling{};

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo m_depthStencil{};

    // Color blending
    VkPipelineColorBlendAttachmentState m_colorBlendAttachment{};

    // Pipeline layout
    std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
    std::vector<VkPushConstantRange> m_pushConstantRanges;
    VkPipelineLayout m_existingLayout = VK_NULL_HANDLE;

    // Created pipeline layout (if we created one)
    VkPipelineLayout m_createdLayout = VK_NULL_HANDLE;
};
