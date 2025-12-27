/**
 * @file pipeline_builder.cpp
 * @brief Implementation of fluent Vulkan pipeline builder
 */

#include "vulkan/pipeline_builder.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

PipelineBuilder::PipelineBuilder(VkDevice device, VkRenderPass renderPass)
    : m_device(device)
    , m_renderPass(renderPass)
{
    clear();
}

PipelineBuilder& PipelineBuilder::clear() {
    // Reset shader state
    m_shaderStages.clear();
    m_vertShaderModule = VK_NULL_HANDLE;
    m_fragShaderModule = VK_NULL_HANDLE;
    m_ownsShaderModules = false;

    // Reset vertex input
    m_vertexBinding = {};
    m_vertexAttributes.clear();
    m_hasVertexInput = false;

    // Default input assembly: triangle list
    m_inputAssembly = {};
    m_inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    m_inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Default viewport (will be set dynamically or fixed later)
    m_viewport = {};
    m_scissor = {};
    m_useDynamicViewport = false;

    // Default rasterization: fill, back-face culling, clockwise front face
    m_rasterizer = {};
    m_rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    m_rasterizer.depthClampEnable = VK_FALSE;
    m_rasterizer.rasterizerDiscardEnable = VK_FALSE;
    m_rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    m_rasterizer.lineWidth = 1.0f;
    m_rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    m_rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    m_rasterizer.depthBiasEnable = VK_FALSE;

    // Default multisampling: none
    m_multisampling = {};
    m_multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    m_multisampling.sampleShadingEnable = VK_FALSE;
    m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Default depth stencil: depth test enabled, write enabled, less comparison
    m_depthStencil = {};
    m_depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_depthStencil.depthTestEnable = VK_TRUE;
    m_depthStencil.depthWriteEnable = VK_TRUE;
    m_depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    m_depthStencil.depthBoundsTestEnable = VK_FALSE;
    m_depthStencil.stencilTestEnable = VK_FALSE;

    // Default color blend: alpha blending enabled
    m_colorBlendAttachment = {};
    m_colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_colorBlendAttachment.blendEnable = VK_TRUE;
    m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    // Reset layout state
    m_descriptorSetLayouts.clear();
    m_pushConstantRanges.clear();
    m_existingLayout = VK_NULL_HANDLE;
    m_createdLayout = VK_NULL_HANDLE;

    return *this;
}

// ========== Shader Configuration ==========

PipelineBuilder& PipelineBuilder::setShaders(const std::string& vertPath,
                                              const std::string& fragPath) {
    auto vertCode = readShaderFile(vertPath);
    auto fragCode = readShaderFile(fragPath);

    m_vertShaderModule = createShaderModule(vertCode);
    m_fragShaderModule = createShaderModule(fragCode);
    m_ownsShaderModules = true;

    return setShaderModules(m_vertShaderModule, m_fragShaderModule);
}

PipelineBuilder& PipelineBuilder::setShaderModules(VkShaderModule vertModule,
                                                    VkShaderModule fragModule) {
    m_shaderStages.clear();

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";
    m_shaderStages.push_back(vertStage);

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";
    m_shaderStages.push_back(fragStage);

    return *this;
}

// ========== Vertex Input Configuration ==========

PipelineBuilder& PipelineBuilder::setVertexInput(
    const VkVertexInputBindingDescription& binding,
    const std::vector<VkVertexInputAttributeDescription>& attributes) {

    m_vertexBinding = binding;
    m_vertexAttributes = attributes;
    m_hasVertexInput = true;
    return *this;
}

PipelineBuilder& PipelineBuilder::setVertexInputType(VertexInputType type) {
    // This would need to be implemented based on actual vertex types in the engine
    // For now, just mark that we want vertex input
    m_hasVertexInput = (type != VertexInputType::None);
    return *this;
}

PipelineBuilder& PipelineBuilder::setNoVertexInput() {
    m_hasVertexInput = false;
    m_vertexAttributes.clear();
    return *this;
}

// ========== Input Assembly ==========

PipelineBuilder& PipelineBuilder::setTopology(VkPrimitiveTopology topology) {
    m_inputAssembly.topology = topology;
    return *this;
}

// ========== Rasterization ==========

PipelineBuilder& PipelineBuilder::setPolygonMode(VkPolygonMode mode) {
    m_rasterizer.polygonMode = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::setCullMode(VkCullModeFlags cullMode,
                                               VkFrontFace frontFace) {
    m_rasterizer.cullMode = cullMode;
    m_rasterizer.frontFace = frontFace;
    return *this;
}

PipelineBuilder& PipelineBuilder::setNoCull() {
    m_rasterizer.cullMode = VK_CULL_MODE_NONE;
    return *this;
}

PipelineBuilder& PipelineBuilder::setLineWidth(float width) {
    m_rasterizer.lineWidth = width;
    return *this;
}

// ========== Depth Testing ==========

PipelineBuilder& PipelineBuilder::setDepthTest(bool depthWrite, VkCompareOp compareOp) {
    m_depthStencil.depthTestEnable = VK_TRUE;
    m_depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    m_depthStencil.depthCompareOp = compareOp;
    return *this;
}

PipelineBuilder& PipelineBuilder::setNoDepthTest() {
    m_depthStencil.depthTestEnable = VK_FALSE;
    m_depthStencil.depthWriteEnable = VK_FALSE;
    return *this;
}

PipelineBuilder& PipelineBuilder::setDepthWrite(bool enabled) {
    m_depthStencil.depthWriteEnable = enabled ? VK_TRUE : VK_FALSE;
    return *this;
}

// ========== Blending ==========

PipelineBuilder& PipelineBuilder::setBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::None:
            return setNoBlending();
        case BlendMode::Alpha:
            return setAlphaBlending();
        case BlendMode::Additive:
            m_colorBlendAttachment.blendEnable = VK_TRUE;
            m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Multiply:
            m_colorBlendAttachment.blendEnable = VK_TRUE;
            m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
            m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }
    return *this;
}

PipelineBuilder& PipelineBuilder::setAlphaBlending() {
    m_colorBlendAttachment.blendEnable = VK_TRUE;
    m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    return *this;
}

PipelineBuilder& PipelineBuilder::setNoBlending() {
    m_colorBlendAttachment.blendEnable = VK_FALSE;
    return *this;
}

// ========== Multisampling ==========

PipelineBuilder& PipelineBuilder::setMultisampling(VkSampleCountFlagBits samples) {
    m_multisampling.rasterizationSamples = samples;
    return *this;
}

PipelineBuilder& PipelineBuilder::setNoMultisampling() {
    m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    m_multisampling.sampleShadingEnable = VK_FALSE;
    return *this;
}

// ========== Pipeline Layout ==========

PipelineBuilder& PipelineBuilder::setDescriptorSetLayout(VkDescriptorSetLayout layout) {
    m_descriptorSetLayouts.clear();
    m_descriptorSetLayouts.push_back(layout);
    return *this;
}

PipelineBuilder& PipelineBuilder::setDescriptorSetLayouts(
    const std::vector<VkDescriptorSetLayout>& layouts) {
    m_descriptorSetLayouts = layouts;
    return *this;
}

PipelineBuilder& PipelineBuilder::addPushConstantRange(VkShaderStageFlags stageFlags,
                                                        uint32_t offset, uint32_t size) {
    VkPushConstantRange range{};
    range.stageFlags = stageFlags;
    range.offset = offset;
    range.size = size;
    m_pushConstantRanges.push_back(range);
    return *this;
}

PipelineBuilder& PipelineBuilder::setPipelineLayout(VkPipelineLayout layout) {
    m_existingLayout = layout;
    return *this;
}

// ========== Dynamic State ==========

PipelineBuilder& PipelineBuilder::setDynamicViewport() {
    m_useDynamicViewport = true;
    return *this;
}

PipelineBuilder& PipelineBuilder::setViewport(float width, float height) {
    m_useDynamicViewport = false;
    m_viewport.x = 0.0f;
    m_viewport.y = 0.0f;
    m_viewport.width = width;
    m_viewport.height = height;
    m_viewport.minDepth = 0.0f;
    m_viewport.maxDepth = 1.0f;

    m_scissor.offset = {0, 0};
    m_scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    return *this;
}

// ========== Build ==========

VkPipeline PipelineBuilder::build(VkPipelineLayout* outLayout) {
    // Create pipeline layout if not provided
    VkPipelineLayout pipelineLayout = m_existingLayout;
    if (pipelineLayout == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = m_descriptorSetLayouts.empty() ? nullptr : m_descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(m_pushConstantRanges.size());
        layoutInfo.pPushConstantRanges = m_pushConstantRanges.empty() ? nullptr : m_pushConstantRanges.data();

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("PipelineBuilder: failed to create pipeline layout!");
        }
        m_createdLayout = pipelineLayout;
    }

    if (outLayout) {
        *outLayout = pipelineLayout;
    }

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (m_hasVertexInput) {
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &m_vertexBinding;
        vertexInputInfo.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(m_vertexAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = m_vertexAttributes.data();
    } else {
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr;
    }

    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    std::vector<VkDynamicState> dynamicStates;
    if (m_useDynamicViewport) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
        viewportState.pViewports = nullptr;
        viewportState.pScissors = nullptr;
    } else {
        viewportState.pViewports = &m_viewport;
        viewportState.pScissors = &m_scissor;
    }

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.empty() ? nullptr : dynamicStates.data();

    // Color blending
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &m_colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStages.size());
    pipelineInfo.pStages = m_shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &m_inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &m_rasterizer;
    pipelineInfo.pMultisampleState = &m_multisampling;
    pipelineInfo.pDepthStencilState = &m_depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = dynamicStates.empty() ? nullptr : &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                   nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("PipelineBuilder: failed to create graphics pipeline!");
    }

    return pipeline;
}

void PipelineBuilder::destroyShaderModules() {
    if (m_ownsShaderModules) {
        if (m_fragShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_fragShaderModule, nullptr);
            m_fragShaderModule = VK_NULL_HANDLE;
        }
        if (m_vertShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_vertShaderModule, nullptr);
            m_vertShaderModule = VK_NULL_HANDLE;
        }
        m_ownsShaderModules = false;
    }
}

// ========== Private Helpers ==========

std::vector<char> PipelineBuilder::readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("PipelineBuilder: failed to open shader file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

VkShaderModule PipelineBuilder::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("PipelineBuilder: failed to create shader module!");
    }

    return shaderModule;
}
