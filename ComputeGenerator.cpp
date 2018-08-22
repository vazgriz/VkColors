#include "ComputeGenerator.h"
#include "Utilities.h"
#include <iostream>
#include <cmath>

#define FRAMES 2
#define GROUP_SIZE 64

ComputeGenerator::ComputeGenerator(Core& core, Allocator& allocator, ColorSource& source, Bitmap& bitmap, ColorQueue& colorQueue, const std::string& shader) : m_staging(core, allocator), m_pyramid(core, allocator, bitmap) {
    m_core = &core;
    m_allocator = &allocator;
    m_source = &source;
    m_bitmap = &bitmap;
    m_colorQueue = &colorQueue;

    m_running = std::make_unique<std::atomic_bool>();

    createCommandPool();
    createCommandBuffers();
    createTexture();
    createTextureView();
    createInputBuffer();
    createReadBuffer();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSet();
    writeDescriptors();
    createMainPipelineLayout();
    createMainPipeline(shader);
    createReducePipelineLayout();
    createReducePipeline();
    createFences();

    glm::ivec2 pos = { static_cast<int>(m_bitmap->width() / 2), static_cast<int>(m_bitmap->height() / 2) };
    if (m_source->hasNext()) {
        m_bitmap->getPixel(pos.x, pos.y) = m_source->getNext();
        addNeighborsToOpenSet(pos);
    }
}

ComputeGenerator::ComputeGenerator(ComputeGenerator&& other) : m_staging(std::move(other.m_staging)), m_pyramid(std::move(other.m_pyramid)) {
    *this = std::move(other);
}

void ComputeGenerator::run() {
    *m_running = true;
    m_thread = std::thread([this]() -> void { generatorLoop(); });
}

void ComputeGenerator::stop() {
    *m_running = false;
    m_thread.join();
}

void ComputeGenerator::generatorLoop() {
    size_t counter = 0;
    std::vector<glm::ivec2> openList;
    Color32 color;
    bool hasChange = false;
    glm::ivec2 changePos;

    while (*m_running) {
        openList.clear();
        for (auto& pos : m_openSet) {
            openList.push_back(pos);
        }

        if (openList.size() == 0) break;
        if (!m_source->hasNext()) break;

        size_t index = m_frame % FRAMES;

        vk::CommandBuffer& commandBuffer = m_commandBuffers[index];
        commandBuffer.reset(vk::CommandBufferResetFlags::None);

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlags::OneTimeSubmit;

        commandBuffer.begin(beginInfo);

        vk::ImageMemoryBarrier barrier = {};
        barrier.image = m_texture.get();
        barrier.oldLayout = vk::ImageLayout::General;
        barrier.newLayout = vk::ImageLayout::TransferDstOptimal;
        barrier.srcAccessMask = vk::AccessFlags::ShaderRead | vk::AccessFlags::ShaderWrite;
        barrier.dstAccessMask = vk::AccessFlags::TransferWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;

        commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::Transfer, vk::DependencyFlags::None,
            {}, {}, { barrier });

        m_staging.transfer(openList.data(), openList.size() * sizeof(glm::ivec2), *m_inputBuffer);

        if (hasChange) {
            vk::Extent3D extent = {};
            extent.width = 1;
            extent.height = 1;
            extent.depth = 1;

            vk::Offset3D offset = {};
            offset.x = changePos.x;
            offset.y = changePos.y;

            m_staging.transfer(&color, sizeof(Color32), *m_texture, vk::ImageLayout::TransferDstOptimal, extent, offset);
        }

        m_staging.flush(commandBuffer);

        barrier.oldLayout = vk::ImageLayout::TransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::General;
        barrier.srcAccessMask = vk::AccessFlags::TransferWrite;
        barrier.dstAccessMask = vk::AccessFlags::ShaderRead | vk::AccessFlags::ShaderWrite;;

        commandBuffer.pipelineBarrier(vk::PipelineStageFlags::Transfer, vk::PipelineStageFlags::ComputeShader, vk::DependencyFlags::None,
            {}, {}, { barrier });

        color = m_source->getNext();
        color.a = 255;

        record(commandBuffer, openList, color);

        commandBuffer.end();

        m_core->submitCompute(commandBuffer, &m_fences[index]);

        m_fences[index].wait();
        m_fences[index].reset();

        hasChange = true;
        changePos = readResult(openList, color);

        m_frame++;
    }
}

struct PushConstants {
    glm::ivec4 color;
    int32_t count;
    int32_t targetLevel;
};

void ComputeGenerator::record(vk::CommandBuffer& commandBuffer, std::vector<glm::ivec2>& openList, Color32 color) {
    if (openList.size() == 0) return;
    commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_mainPipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::Compute, *m_mainPipelineLayout, 0, { m_pyramid.descriptorSet(), *m_descriptorSet }, {});

    PushConstants constants = {};
    constants.count = static_cast<uint32_t>(openList.size());
    constants.targetLevel = static_cast<uint32_t>(ceil(log2(openList.size())));
    constants.color = glm::ivec4{ color.r, color.g, color.b, 255 };

    commandBuffer.pushConstants(*m_mainPipelineLayout, vk::ShaderStageFlags::Compute, 0, sizeof(PushConstants), &constants);

    uint32_t mainGroups = (static_cast<uint32_t>(openList.size()) / GROUP_SIZE) + 1;
    commandBuffer.dispatch(mainGroups, 1, 1);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_reducePipeline);

    vk::BufferMemoryBarrier barrier = {};
    barrier.buffer = &m_pyramid.buffers()[constants.targetLevel];
    barrier.size = m_pyramid.buffers()[constants.targetLevel].size();
    barrier.dstAccessMask = vk::AccessFlags::ShaderWrite;
    barrier.srcAccessMask = vk::AccessFlags::ShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::ComputeShader, vk::DependencyFlags::None,
        {}, { barrier }, {});

    uint32_t currentCount = constants.count;
    for (uint32_t currentLevel = constants.targetLevel; currentLevel > 0; currentLevel--) {
        constants.count = currentCount;
        constants.targetLevel = currentLevel - 1;

        commandBuffer.pushConstants(*m_mainPipelineLayout, vk::ShaderStageFlags::Compute, 0, 2 * sizeof(int32_t), &constants.count);

        uint32_t reduceGroups = (static_cast<uint32_t>(currentCount) / GROUP_SIZE) + 1;
        commandBuffer.dispatch(reduceGroups, 1, 1);

        barrier.buffer = &m_pyramid.buffers()[constants.targetLevel];
        barrier.size = m_pyramid.buffers()[constants.targetLevel].size();
        commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::ComputeShader, vk::DependencyFlags::None,
            {}, { barrier }, {});

        currentCount = (currentCount / 2) + (currentCount % 2);
    }

    vk::BufferCopy copy = {};
    copy.size = m_readBuffer->size();
    commandBuffer.copyBuffer(m_pyramid.buffers()[0], *m_readBuffer, { copy });

    barrier.buffer = m_readBuffer.get();
    barrier.size = m_readBuffer->size();
    barrier.srcAccessMask = vk::AccessFlags::TransferWrite;
    barrier.dstAccessMask = vk::AccessFlags::HostRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::Transfer, vk::PipelineStageFlags::Host, vk::DependencyFlags::None,
        {}, { barrier }, {});
}

glm::ivec2 ComputeGenerator::readResult(std::vector<glm::ivec2>& openList, Color32 color) {
    uint32_t result;
    memcpy(&result, m_resultMapping, sizeof(uint32_t));
    glm::ivec2 pos = openList[result];
    m_colorQueue->enqueue(pos, color);
    m_bitmap->getPixel(pos.x, pos.y) = color;
    addNeighborsToOpenSet(pos);
    m_openSet.erase(pos);
    return pos;
}

void ComputeGenerator::addToOpenSet(glm::ivec2 pos) {
    m_openSet.insert(pos);
}

void ComputeGenerator::addNeighborsToOpenSet(glm::ivec2 pos) {
    glm::ivec2 neighbors[8] = {
        pos + glm::ivec2{ -1, -1 },
        pos + glm::ivec2{ -1,  0 },
        pos + glm::ivec2{ -1,  1 },
        pos + glm::ivec2{  0, -1 },
        pos + glm::ivec2{  0,  1 },
        pos + glm::ivec2{  1, -1 },
        pos + glm::ivec2{  1,  0 },
        pos + glm::ivec2{  1,  1 },
    };

    for (size_t i = 0; i < 8; i++) {
        auto& n = neighbors[i];
        if (n.x >= 0 && n.y >= 0
            && n.x < m_bitmap->width() && n.y < m_bitmap->height()
            && m_bitmap->getPixel(n.x, n.y).a == 0) {
            addToOpenSet(n);
        }
    }
}

void ComputeGenerator::createCommandPool() {
    vk::CommandPoolCreateInfo info = {};
    info.queueFamilyIndex = m_core->computeQueueFamilyIndex();
    info.flags = vk::CommandPoolCreateFlags::ResetCommandBuffer;

    m_commandPool = std::make_unique<vk::CommandPool>(m_core->device(), info);
}

void ComputeGenerator::createCommandBuffers() {
    vk::CommandBufferAllocateInfo info = {};
    info.commandPool = m_commandPool.get();
    info.commandBufferCount = FRAMES;

    m_commandBuffers = m_commandPool->allocate(info);
}

void ComputeGenerator::createTexture() {
    vk::ImageCreateInfo info = {};
    info.format = vk::Format::R8G8B8A8_Uint;
    info.extent = { static_cast<uint32_t>(m_bitmap->width()), static_cast<uint32_t>(m_bitmap->height()), 1 };
    info.arrayLayers = 1;
    info.imageType = vk::ImageType::_2D;
    info.initialLayout = vk::ImageLayout::Undefined;
    info.mipLevels = 1;
    info.samples = vk::SampleCountFlags::_1;
    info.usage = vk::ImageUsageFlags::Storage | vk::ImageUsageFlags::TransferDst;

    m_texture = std::make_unique<vk::Image>(m_core->device(), info);

    Allocation alloc = m_allocator->allocate(m_texture->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_texture->bind(*alloc.memory, alloc.size);

    vk::CommandBuffer commandBuffer = m_core->getSingleUseCommandBuffer();

    vk::ImageMemoryBarrier barrier = {};
    barrier.image = m_texture.get();
    barrier.oldLayout = vk::ImageLayout::Undefined;
    barrier.newLayout = vk::ImageLayout::General;
    barrier.srcAccessMask = vk::AccessFlags::None;
    barrier.dstAccessMask = vk::AccessFlags::ShaderRead | vk::AccessFlags::ShaderWrite;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::TopOfPipe, vk::PipelineStageFlags::ComputeShader, vk::DependencyFlags::None,
        {}, {}, { barrier });

    m_core->submitSingleUseCommandBuffer(std::move(commandBuffer));
}

void ComputeGenerator::createTextureView() {
    vk::ImageViewCreateInfo info = {};
    info.image = m_texture.get();
    info.format = m_texture->format();
    info.viewType = vk::ImageViewType::_2D;
    info.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;

    m_textureView = std::make_unique<vk::ImageView>(m_core->device(), info);
}

void ComputeGenerator::createInputBuffer() {
    vk::BufferCreateInfo info = {};
    info.size = sizeof(glm::uvec2) * m_bitmap->width() * m_bitmap->height();
    info.usage = vk::BufferUsageFlags::StorageBuffer | vk::BufferUsageFlags::TransferDst;

    m_inputBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

    Allocation alloc = m_allocator->allocate(m_inputBuffer->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_inputBuffer->bind(*alloc.memory, alloc.offset);
}

void ComputeGenerator::createReadBuffer() {
    vk::BufferCreateInfo info = {};
    info.size = sizeof(uint32_t);
    info.usage = vk::BufferUsageFlags::TransferDst;

    m_readBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

    m_readAlloc = m_allocator->allocate(m_readBuffer->requirements(),
        vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent | vk::MemoryPropertyFlags::HostCached,
        vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent);
    m_readBuffer->bind(*m_readAlloc.memory, m_readAlloc.offset);
    m_resultMapping = m_readAlloc.memory->map(m_readAlloc.offset, m_readAlloc.size);
}

void ComputeGenerator::createDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding binding1 = {};
    binding1.binding = 0;
    binding1.descriptorType = vk::DescriptorType::StorageImage;
    binding1.descriptorCount = 1;
    binding1.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutBinding binding2 = {};
    binding2.binding = 1;
    binding2.descriptorType = vk::DescriptorType::StorageBuffer;
    binding2.descriptorCount = 1;
    binding2.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindings = { binding1, binding2 };

    m_descriptorSetLayout = std::make_unique<vk::DescriptorSetLayout>(m_core->device(), info);
}

void ComputeGenerator::createDescriptorPool() {
    vk::DescriptorPoolSize size1 = {};
    size1.type = vk::DescriptorType::StorageImage;
    size1.descriptorCount = 1;

    vk::DescriptorPoolSize size2 = {};
    size2.type = vk::DescriptorType::StorageBuffer;
    size2.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo info = {};
    info.maxSets = 1;
    info.poolSizes = { size1, size2 };

    m_descriptorPool = std::make_unique<vk::DescriptorPool>(m_core->device(), info);
}

void ComputeGenerator::createDescriptorSet() {
    vk::DescriptorSetAllocateInfo info = {};
    info.descriptorPool = m_descriptorPool.get();
    info.setLayouts = { *m_descriptorSetLayout };
    
    m_descriptorSet = std::make_unique<vk::DescriptorSet>(std::move(m_descriptorPool->allocate(info)[0]));
}

void ComputeGenerator::writeDescriptors() {
    vk::DescriptorImageInfo imageInfo = {};
    imageInfo.imageView = m_textureView.get();
    imageInfo.imageLayout = vk::ImageLayout::General;

    vk::DescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = m_inputBuffer.get();
    bufferInfo.range = m_inputBuffer->size();

    vk::WriteDescriptorSet write1 = {};
    write1.dstSet = m_descriptorSet.get();
    write1.dstBinding = 0;
    write1.imageInfo = { imageInfo };
    write1.descriptorType = vk::DescriptorType::StorageImage;

    vk::WriteDescriptorSet write2 = {};
    write2.dstSet = m_descriptorSet.get();
    write2.dstBinding = 1;
    write2.bufferInfo = { bufferInfo };
    write2.descriptorType = vk::DescriptorType::StorageBuffer;

    vk::DescriptorSet::update(m_core->device(), { write1, write2 }, {});
}

void ComputeGenerator::createMainPipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = sizeof(PushConstants);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { m_pyramid.descriptorSetLayout(), *m_descriptorSetLayout };
    info.pushConstantRanges = { range };
    
    m_mainPipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void ComputeGenerator::createMainPipeline(const std::string& shader) {
    vk::ShaderModule module = loadShader(m_core->device(), shader);

    uint32_t specData[2] = { GROUP_SIZE, static_cast<uint32_t>(m_pyramid.buffers().size()) };

    vk::SpecializationMapEntry entry1 = {};
    entry1.constantID = 0;
    entry1.size = sizeof(uint32_t);
    entry1.offset = 0;

    vk::SpecializationMapEntry entry2 = {};
    entry2.constantID = 1;
    entry2.size = sizeof(uint32_t);
    entry2.offset = sizeof(uint32_t);

    vk::SpecializationInfo specInfo = {};
    specInfo.dataSize = sizeof(specData);
    specInfo.data = specData;
    specInfo.mapEntries = { entry1, entry2 };

    vk::PipelineShaderStageCreateInfo shaderInfo = {};
    shaderInfo.module = &module;
    shaderInfo.name = "main";
    shaderInfo.stage = vk::ShaderStageFlags::Compute;
    shaderInfo.specializationInfo = &specInfo;

    vk::ComputePipelineCreateInfo info = {};
    info.stage = shaderInfo;
    info.layout = m_mainPipelineLayout.get();

    m_mainPipeline = std::make_unique<vk::ComputePipeline>(m_core->device(), info);
}

void ComputeGenerator::createReducePipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = 2 * sizeof(int32_t);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { m_pyramid.descriptorSetLayout() };
    info.pushConstantRanges = { range };

    m_reducePipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void ComputeGenerator::createReducePipeline() {
    vk::ShaderModule module = loadShader(m_core->device(), "shaders/reduce.comp.spv");

    uint32_t specData[2] = { GROUP_SIZE, static_cast<uint32_t>(m_pyramid.buffers().size()) };

    vk::SpecializationMapEntry entry1 = {};
    entry1.constantID = 0;
    entry1.size = sizeof(uint32_t);
    entry1.offset = 0;

    vk::SpecializationMapEntry entry2 = {};
    entry2.constantID = 1;
    entry2.size = sizeof(uint32_t);
    entry2.offset = sizeof(uint32_t);

    vk::SpecializationInfo specInfo = {};
    specInfo.dataSize = sizeof(specData);
    specInfo.data = specData;
    specInfo.mapEntries = { entry1, entry2 };

    vk::PipelineShaderStageCreateInfo shaderInfo = {};
    shaderInfo.module = &module;
    shaderInfo.name = "main";
    shaderInfo.stage = vk::ShaderStageFlags::Compute;
    shaderInfo.specializationInfo = &specInfo;

    vk::ComputePipelineCreateInfo info = {};
    info.stage = shaderInfo;
    info.layout = m_reducePipelineLayout.get();

    m_reducePipeline = std::make_unique<vk::ComputePipeline>(m_core->device(), info);
}

void ComputeGenerator::createFences() {
    vk::FenceCreateInfo info = {};
    //info.flags = vk::FenceCreateFlags::Signaled;

    for (size_t i = 0; i < FRAMES; i++) {
        m_fences.emplace_back(m_core->device(), info);
    }
}