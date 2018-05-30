#include "Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
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

Renderer::Renderer(Core& core, Allocator& allocator, int32_t width, int32_t height) : bitmap(width, height) {
    m_core = &core;
    m_allocator = &allocator;
    m_width = width;
    m_height = height;
    m_core->registerObserver(this);

    vk::CommandBuffer commandBuffer = m_core->getSingleUseCommandBuffer();

    createVertexBuffer(commandBuffer);
    createIndexBuffer(commandBuffer);
    createTexture(commandBuffer);

    m_core->submitSingleUseCommandBuffer(std::move(commandBuffer));

    createTextureView();
    createSampler();
    createDescriptorPool();
    createDescriptorSet();
    createPipelineLayout();
    createPipeline();

    int32_t wWidth = static_cast<int32_t>(m_core->swapchain().extent().width);
    int32_t wHeight = static_cast<int32_t>(m_core->swapchain().extent().height);
    onResize(wWidth, wHeight);
}

Renderer::Renderer(Renderer&& other) : bitmap(std::move(other.bitmap)) {
    *this = std::move(other);
}

void Renderer::record(vk::CommandBuffer& commandBuffer) {
    commandBuffer.bindVertexBuffers(0, { *m_vertexBuffer }, { 0 });
    commandBuffer.bindIndexBuffer(*m_indexBuffer, 0, vk::IndexType::Uint32);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::Graphics, *m_pipeline);
    commandBuffer.pushConstants(*m_pipelineLayout, vk::ShaderStageFlags::Vertex, 0, sizeof(glm::mat4), &projectionMatrix);
    commandBuffer.drawIndexed(6, 1, 0, 0, 0);
}

void Renderer::onResize(int width, int height) {
    projectionMatrix = glm::ortho(-m_width / 2.0f, m_width / 2.0f, -m_height / 2.0f, m_height / 2.0f, 0.0f, 1.0f);
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
    vk::ImageCreateInfo info = {};
    info.extent.width = static_cast<uint32_t>(bitmap.width());
    info.extent.height = static_cast<uint32_t>(bitmap.height());
    info.extent.depth = 1;
    info.format = vk::Format::R8G8B8A8_Unorm;
    info.initialLayout = vk::ImageLayout::Undefined;
    info.arrayLayers = 1;
    info.mipLevels = 1;
    info.imageType = vk::ImageType::_2D;
    info.samples = vk::SampleCountFlags::_1;
    info.usage = vk::ImageUsageFlags::Sampled | vk::ImageUsageFlags::TransferDst;

    m_texture = std::make_unique<vk::Image>(m_core->device(), info);

    m_textureAlloc = m_allocator->allocate(m_texture->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_texture->bind(*m_textureAlloc.memory, m_textureAlloc.offset);

    vk::ImageMemoryBarrier barrier = {};
    barrier.image = m_texture.get();
    barrier.oldLayout = vk::ImageLayout::Undefined;
    barrier.newLayout = vk::ImageLayout::TransferDstOptimal;
    barrier.srcAccessMask = vk::AccessFlags::None;
    barrier.dstAccessMask = vk::AccessFlags::TransferWrite;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::TopOfPipe, vk::PipelineStageFlags::Transfer, vk::DependencyFlags::None,
        {}, {}, { barrier });

    m_allocator->transfer(bitmap.data(), bitmap.size(), *m_texture, vk::ImageLayout::TransferDstOptimal);
    m_allocator->flushStaging(commandBuffer);

    barrier.oldLayout = vk::ImageLayout::TransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::ShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlags::TransferWrite;
    barrier.dstAccessMask = vk::AccessFlags::ShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::Transfer, vk::PipelineStageFlags::FragmentShader, vk::DependencyFlags::None,
        {}, {}, { barrier });
}

void Renderer::createTextureView() {
    vk::ImageViewCreateInfo info = {};
    info.image = m_texture.get();
    info.format = m_texture->format();
    info.viewType = vk::ImageViewType::_2D;
    info.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;

    m_textureView = std::make_unique<vk::ImageView>(m_core->device(), info);
}

void Renderer::createSampler() {
    vk::SamplerCreateInfo info = {};
    info.addressModeU = vk::SamplerAddressMode::Repeat;
    info.addressModeV = vk::SamplerAddressMode::Repeat;
    info.addressModeW = vk::SamplerAddressMode::Repeat;
    info.magFilter = vk::Filter::Nearest;
    info.minFilter = vk::Filter::Nearest;
    info.maxAnisotropy = 1;
    
    m_sampler = std::make_unique<vk::Sampler>(m_core->device(), info);
}

void Renderer::createDescriptorPool() {

}

void Renderer::createDescriptorSet() {

}

void Renderer::createPipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = sizeof(glm::mat4);
    range.stageFlags = vk::ShaderStageFlags::Vertex;

    vk::PipelineLayoutCreateInfo info = {};
    info.pushConstantRanges = { range };
    
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
    viewport.width = static_cast<float>(m_core->swapchain().extent().width);
    viewport.height = static_cast<float>(m_core->swapchain().extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = m_core->swapchain().extent();

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