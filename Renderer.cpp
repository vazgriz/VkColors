#include "Renderer.h"
#include <glm/glm.hpp>
#include "Utilities.h"

struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;

    static std::vector<vk::VertexInputAttributeDescription> getAttributes() {
        vk::VertexInputAttributeDescription posA = {};
        posA.binding = 0;
        posA.format = vk::Format::R32G32B32_Sfloat;
        posA.location = 0;
        posA.offset = offsetof(Vertex, pos);

        vk::VertexInputAttributeDescription uvA = {};
        uvA.binding = 0;
        uvA.format = vk::Format::R32G32_Sfloat;
        uvA.location = 1;
        uvA.offset = offsetof(Vertex, uv);

        return { posA, uvA };
    }

    static std::vector<vk::VertexInputBindingDescription> getBindings() {
        vk::VertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.inputRate = vk::VertexInputRate::Vertex;
        binding.stride = sizeof(Vertex);

        return { binding };
    }
};

Renderer::Renderer(Core& core, Allocator& allocator, int32_t width, int32_t height) {
    m_core = &core;
    m_allocator = &allocator;
    m_width = width;
    m_height = height;

    vk::CommandBuffer commandBuffer = m_core->getSingleUseCommandBuffer();

    createVertexBuffer(commandBuffer);
    createIndexBuffer(commandBuffer);
    createTexture(commandBuffer);

    m_allocator->flushStaging(commandBuffer);
    m_core->submitSingleUseCommandBuffer(std::move(commandBuffer));

    createTextureView();
    createDescriptorPool();
    createDescriptorSet();
    createPipelineLayout();
    createPipeline();
}

Renderer::Renderer(Renderer&& other) {
    *this = std::move(other);
}

void Renderer::record(vk::CommandBuffer& commandBuffer) {
    commandBuffer.bindVertexBuffers(0, { *m_vertexBuffer }, { 0 });
    commandBuffer.bindIndexBuffer(*m_indexBuffer, 0, vk::IndexType::Uint32);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::Graphics, *m_pipeline);
    commandBuffer.drawIndexed(6, 1, 0, 0, 0);
}

void Renderer::createVertexBuffer(vk::CommandBuffer& commandBuffer) {
    std::vector<Vertex> vertices = {
        { { -m_width / 2, -m_height / 2, 0 }, { 0, 0 } },
        { {  m_width / 2, -m_height / 2, 0 }, { 0, 0 } },
        { { -m_width / 2,  m_height / 2, 0 }, { 0, 0 } },
        { {  m_width / 2,  m_height / 2, 0 }, { 0, 0 } },
    };

    vk::BufferCreateInfo info = {};
    info.size = vertices.size() * sizeof(Vertex);
    info.usage = vk::BufferUsageFlags::VertexBuffer | vk::BufferUsageFlags::TransferDst;

    m_vertexBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

    m_vertexAlloc = m_allocator->allocate(m_vertexBuffer->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_vertexBuffer->bind(*m_vertexAlloc.memory, m_vertexAlloc.offset);

    m_allocator->transfer(vertices.data(), info.size, *m_vertexBuffer);
}

void Renderer::createIndexBuffer(vk::CommandBuffer& commandBuffer) {
    std::vector<uint32_t> indices = {
        0, 1, 2,
        1, 3, 2
    };

    vk::BufferCreateInfo info = {};
    info.size = indices.size() * sizeof(uint32_t);
    info.usage = vk::BufferUsageFlags::IndexBuffer | vk::BufferUsageFlags::TransferDst;

    m_indexBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

    m_indexAlloc = m_allocator->allocate(m_indexBuffer->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_indexBuffer->bind(*m_indexAlloc.memory, m_indexAlloc.offset);

    m_allocator->transfer(indices.data(), info.size, *m_indexBuffer);
}

void Renderer::createTexture(vk::CommandBuffer& commandBuffer) {

}

void Renderer::createTextureView() {

}

void Renderer::createDescriptorPool() {

}

void Renderer::createDescriptorSet() {

}

void Renderer::createPipelineLayout() {
    vk::PipelineLayoutCreateInfo info = {};
    
    m_pipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void Renderer::createPipeline() {
    vk::ShaderModule vertShader = loadShader(m_core->device(), "shaders/shader.vert.spv");
    vk::ShaderModule fragShader = loadShader(m_core->device(), "shaders/shader.frag.spv");

    vk::PipelineShaderStageCreateInfo vertInfo = {};
    vertInfo.module = &vertShader;
    vertInfo.name = "main";
    vertInfo.stage = vk::ShaderStageFlags::Vertex;

    vk::PipelineShaderStageCreateInfo fragInfo = {};
    fragInfo.module = &fragShader;
    fragInfo.name = "main";
    fragInfo.stage = vk::ShaderStageFlags::Fragment;

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.vertexAttributeDescriptions = Vertex::getAttributes();
    vertexInputInfo.vertexBindingDescriptions = Vertex::getBindings();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.topology = vk::PrimitiveTopology::TriangleList;

    vk::Viewport viewport = {};
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };

    vk::PipelineViewportStateCreateInfo viewportState = {};
    viewportState.viewports = { viewport };
    viewportState.scissors = { scissor };

    vk::PipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.polygonMode = vk::PolygonMode::Fill;
    rasterizer.lineWidth = 1;
    rasterizer.cullMode = vk::CullModeFlags::Back;
    rasterizer.frontFace = vk::FrontFace::Clockwise;

    vk::PipelineMultisampleStateCreateInfo multisample = {};
    multisample.rasterizationSamples = vk::SampleCountFlags::_1;

    vk::PipelineColorBlendAttachmentState colorAttachment = {};
    colorAttachment.colorWriteMask = vk::ColorComponentFlags::R | vk::ColorComponentFlags::G | vk::ColorComponentFlags::B | vk::ColorComponentFlags::A;

    vk::PipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.attachments = { colorAttachment };

    vk::GraphicsPipelineCreateInfo info = {};
    info.stages = { vertInfo, fragInfo };
    info.vertexInputState = &vertexInputInfo;
    info.inputAssemblyState = &inputAssembly;
    info.viewportState = &viewportState;
    info.rasterizationState = &rasterizer;
    info.multisampleState = &multisample;
    info.colorBlendState = &colorBlending;
    info.layout = m_pipelineLayout.get();
    info.renderPass = &m_core->renderPass();
    info.subpass = 0;
    
    m_pipeline = std::make_unique<vk::GraphicsPipeline>(m_core->device(), info);
}