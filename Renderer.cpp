#include "Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Utilities.h"

#define STAGING_SIZE (64 * 1024 * 1024)

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

Renderer::Renderer(Core& core, Allocator& allocator, glm::ivec2 size, ColorQueue& colorQueue) : m_staging(core, allocator, STAGING_SIZE) {
    m_core = &core;
    m_allocator = &allocator;
    m_queue = &colorQueue;
    m_size = size;
    m_core->registerObserver(this);

    vk::CommandBuffer commandBuffer = m_core->getSingleUseCommandBuffer();

    createVertexBuffer(commandBuffer);
    createIndexBuffer(commandBuffer);
    createTexture(commandBuffer);
    m_staging.flush(commandBuffer);

    m_core->submitSingleUseCommandBuffer(std::move(commandBuffer));

    createTextureView();
    createSampler();
    createDescriptorLayout();
    createDescriptorPool();
    createDescriptorSet();
    createPipelineLayout();
    createPipeline();

    int32_t wWidth = static_cast<int32_t>(m_core->swapchain().extent().width);
    int32_t wHeight = static_cast<int32_t>(m_core->swapchain().extent().height);
    onResize(wWidth, wHeight);
}

Renderer::Renderer(Renderer&& other) : m_staging(std::move(other.m_staging)) {
    *this = std::move(other);
}

void Renderer::record(vk::CommandBuffer& commandBuffer) {
    vk::ImageMemoryBarrier barrier = {};
    barrier.image = m_texture.get();
    barrier.oldLayout = vk::ImageLayout::ShaderReadOnlyOptimal;
    barrier.newLayout = vk::ImageLayout::TransferDstOptimal;
    barrier.srcAccessMask = vk::AccessFlags::ShaderRead;
    barrier.dstAccessMask = vk::AccessFlags::TransferWrite;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::FragmentShader, vk::PipelineStageFlags::Transfer, vk::DependencyFlags::None,
        {}, {}, { barrier });

    auto changes = m_queue->swap();
    
    for (auto item : changes) {
        vk::Extent3D extent = {};
        extent.width = 1;
        extent.height = 1;
        extent.depth = 1;
    
        vk::Offset3D offset = {};
        offset.x = item.pos.x;
        offset.y = item.pos.y;
    
        m_staging.transfer(&item.color, sizeof(Color32), *m_texture, vk::ImageLayout::TransferDstOptimal, extent, offset);
    }

    m_staging.flush(commandBuffer);

    barrier.oldLayout = vk::ImageLayout::TransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::ShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlags::TransferWrite;
    barrier.dstAccessMask = vk::AccessFlags::ShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::Transfer, vk::PipelineStageFlags::FragmentShader, vk::DependencyFlags::None,
        {}, {}, { barrier });

    m_core->beginRenderPass(commandBuffer);

    commandBuffer.bindVertexBuffers(0, { *m_vertexBuffer }, { 0 });
    commandBuffer.bindIndexBuffer(*m_indexBuffer, 0, vk::IndexType::Uint32);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::Graphics, *m_pipelineLayout, 0, { *m_descriptorSet }, {});
    commandBuffer.bindPipeline(vk::PipelineBindPoint::Graphics, *m_pipeline);
    commandBuffer.pushConstants(*m_pipelineLayout, vk::ShaderStageFlags::Vertex, 0, sizeof(glm::mat4), &m_projectionMatrix);
    commandBuffer.drawIndexed(6, 1, 0, 0, 0);

    commandBuffer.endRenderPass();
}

void Renderer::onResize(int width, int height) {
    m_projectionMatrix = glm::ortho<float>(-width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f, 0, 1);
    if (m_pipeline != nullptr) {
        createPipeline();
    }
}

void Renderer::createVertexBuffer(vk::CommandBuffer& commandBuffer) {
    std::vector<Vertex> vertices = {
        { { -m_size.x / 2, -m_size.y / 2, 0 }, { 0, 0 } },
        { {  m_size.x / 2, -m_size.y / 2, 0 }, { 1, 0 } },
        { { -m_size.x / 2,  m_size.y / 2, 0 }, { 0, 1 } },
        { {  m_size.x / 2,  m_size.y / 2, 0 }, { 1, 1 } },
    };

    vk::BufferCreateInfo info = {};
    info.size = vertices.size() * sizeof(Vertex);
    info.usage = vk::BufferUsageFlags::VertexBuffer | vk::BufferUsageFlags::TransferDst;

    m_vertexBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

    m_vertexAlloc = m_allocator->allocate(m_vertexBuffer->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_vertexBuffer->bind(*m_vertexAlloc.memory, m_vertexAlloc.offset);

    m_staging.transfer(vertices.data(), info.size, *m_vertexBuffer);
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

    m_staging.transfer(indices.data(), info.size, *m_indexBuffer);
}

void Renderer::createTexture(vk::CommandBuffer& commandBuffer) {
    vk::ImageCreateInfo info = {};
    info.extent.width = static_cast<uint32_t>(m_size.x);
    info.extent.height = static_cast<uint32_t>(m_size.y);
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
    barrier.newLayout = vk::ImageLayout::ShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlags::None;
    barrier.dstAccessMask = vk::AccessFlags::ShaderRead;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::TopOfPipe, vk::PipelineStageFlags::FragmentShader, vk::DependencyFlags::None,
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

void Renderer::createDescriptorLayout() {
    vk::DescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorCount = 1;
    binding.descriptorType = vk::DescriptorType::CombinedImageSampler;
    binding.stageFlags = vk::ShaderStageFlags::Fragment;

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindings = { binding };

    m_descriptorLayout = std::make_unique<vk::DescriptorSetLayout>(m_core->device(), info);
}

void Renderer::createDescriptorPool() {
    vk::DescriptorPoolSize poolSize = {};
    poolSize.descriptorCount = 1;
    poolSize.type = vk::DescriptorType::CombinedImageSampler;

    vk::DescriptorPoolCreateInfo info = {};
    info.maxSets = 1;
    info.poolSizes = { poolSize };
    
    m_descriptorPool = std::make_unique<vk::DescriptorPool>(m_core->device(), info);
}

void Renderer::createDescriptorSet() {
    vk::DescriptorSetAllocateInfo info = {};
    info.descriptorPool = m_descriptorPool.get();
    info.setLayouts = { *m_descriptorLayout };
    
    m_descriptorSet = std::make_unique<vk::DescriptorSet>(std::move(m_descriptorPool->allocate(info)[0]));

    vk::DescriptorImageInfo imageInfo = {};
    imageInfo.sampler = m_sampler.get();
    imageInfo.imageView = m_textureView.get();
    imageInfo.imageLayout = vk::ImageLayout::ShaderReadOnlyOptimal;

    vk::WriteDescriptorSet write = {};
    write.dstSet = m_descriptorSet.get();
    write.descriptorType = vk::DescriptorType::CombinedImageSampler;
    write.imageInfo = { imageInfo };

    vk::DescriptorSet::update(m_core->device(), { write }, {});
}

void Renderer::createPipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = sizeof(glm::mat4);
    range.stageFlags = vk::ShaderStageFlags::Vertex;

    vk::PipelineLayoutCreateInfo info = {};
    info.pushConstantRanges = { range };
    info.setLayouts = { *m_descriptorLayout };
    
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