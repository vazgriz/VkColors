#include "ComputeGenerator.h"
#include "Utilities.h"
#include <iostream>
#include <cmath>

#define FRAMES 2

ComputeGenerator::ComputeGenerator(Core& core, Allocator& allocator, ColorSource& source, ColorQueue& colorQueue, Options& options)
    : m_bitmap(options.size.x, options.size.y) {
    m_core = &core;
    m_allocator = &allocator;
    m_source = &source;
    m_size = options.size;
    m_colorQueue = &colorQueue;
    m_frameData.resize(FRAMES);

    m_running = std::make_unique<std::atomic_bool>();

    m_workGroupSize = 64;
    m_maxBatchAbsolute = 1024;
    m_maxBatchRelative = 1024;

    createCommandPool();
    createCommandBuffers();
    createTexture();
    createTextureView();
    createPositionBuffers();
    createColorBuffers();
    createOutputBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    writeDescriptors();
    createUpdatePipelineLayout();
    createUpdatePipeline();
    createMainPipelineLayout();
    createMainPipeline(options.shader);
    createFences();

    glm::ivec2 pos = m_size / 2;
    if (m_source->hasNext()) {
        Color32 color = m_source->getNext();
        m_queue.push({ color, pos });
        m_colorQueue->enqueue(pos, color);
        addNeighborsToOpenSet(pos);
    }
}

ComputeGenerator::ComputeGenerator(ComputeGenerator&& other) : m_bitmap(std::move(other.m_bitmap)) {
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
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
    std::vector<std::vector<glm::ivec2>> openLists(FRAMES);
    std::vector<std::vector<Color32>> colors(FRAMES);

    while (*m_running) {
        size_t index = m_frame % FRAMES;

        auto& openList = openLists[index];
        auto& colorList = colors[index];
        auto& frameData = m_frameData[index];

        if (m_openSet.size() == 0) break;
        if (!m_source->hasNext()) break;

        m_fences[index].wait();
        m_fences[index].reset();

        if (m_frame >= FRAMES) {
            readResult(index, openList, colorList);
            colorList.clear();
        }

        openList.clear();

        for (auto& pos : m_openSet) {
            openList.push_back(pos);
        }

        memcpy(frameData.positionMapping, openList.data(), openList.size() * sizeof(glm::ivec2));

        uint32_t batchSize = std::max<uint32_t>(1, std::min<uint32_t>(m_maxBatchAbsolute, static_cast<uint32_t>(openList.size() / m_maxBatchRelative)));
        glm::ivec4* colorPtr = static_cast<glm::ivec4*>(frameData.colorMapping);

        for (uint32_t i = 0; i < batchSize; i++) {
            if (m_source->hasNext()) {
                Color32 color = m_source->getNext();
                colorList.push_back(color);
                colorPtr[i] = glm::ivec4(color.r, color.g, color.b, 255);
            } else {
                batchSize = i;
                break;
            }
        }

        vk::CommandBuffer& commandBuffer = *frameData.commandBuffer;
        commandBuffer.reset(vk::CommandBufferResetFlags::None);

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlags::OneTimeSubmit;

        commandBuffer.begin(beginInfo);

        record(commandBuffer, openList, colorList, index, batchSize);

        commandBuffer.end();

        m_core->submitCompute(commandBuffer, &m_fences[index]);

        m_frame++;
    }

    for (size_t i = 0; i < FRAMES; i++) {
        m_frame++;
        size_t index = m_frame % FRAMES;

        auto& openList = openLists[index];
        auto& color = colors[index];

        m_fences[index].wait();
        m_fences[index].reset();

        if (m_frame >= FRAMES) {
            readResult(index, openList, color);
        }
    }
}

struct UpdatePushConstants {
    glm::ivec4 color;
    glm::ivec2 pos;
};

struct MainPushConstants {
    uint32_t count;
};

void ComputeGenerator::record(vk::CommandBuffer& commandBuffer, std::vector<glm::ivec2>& openList, std::vector<Color32>& colors, size_t index, uint32_t batchSize) {
    auto& frameData = m_frameData[index];

    vk::ImageMemoryBarrier barrier = {};
    barrier.image = m_texture.get();
    barrier.oldLayout = vk::ImageLayout::General;
    barrier.newLayout = vk::ImageLayout::General;
    barrier.srcAccessMask = vk::AccessFlags::ShaderRead;
    barrier.dstAccessMask = vk::AccessFlags::ShaderWrite;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::ComputeShader, {}, {}, {}, { barrier });

    //update image
    while (m_queue.size() > 0) {
        auto item = m_queue.front();
        m_queue.pop();

        commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_updatePipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::Compute, *m_updatePipelineLayout, 0, { *frameData.descriptor }, {});

        UpdatePushConstants updateConstants = {};
        updateConstants.color = glm::ivec4{ item.color.r, item.color.g, item.color.b, 255 };
        updateConstants.pos = item.pos;

        commandBuffer.pushConstants(*m_updatePipelineLayout, vk::ShaderStageFlags::Compute, 0, sizeof(UpdatePushConstants), &updateConstants);
        commandBuffer.dispatch(1, 1, 1);
    }

    barrier.srcAccessMask = vk::AccessFlags::ShaderWrite;
    barrier.dstAccessMask = vk::AccessFlags::ShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::ComputeShader, {}, {}, {}, { barrier });

    commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_mainPipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::Compute, *m_mainPipelineLayout, 0, { *frameData.descriptor }, {});

    MainPushConstants mainConstants = {};
    mainConstants.count = static_cast<uint32_t>(openList.size());

    commandBuffer.pushConstants(*m_mainPipelineLayout, vk::ShaderStageFlags::Compute, 0, sizeof(MainPushConstants), &mainConstants);

    uint32_t mainGroups = getWorkGroupCount(openList.size());
    commandBuffer.dispatch(mainGroups, batchSize, 1);

    vk::BufferMemoryBarrier bufferBarrier = {};
    bufferBarrier.buffer = frameData.outputBuffer.get();
    bufferBarrier.size = VK_WHOLE_SIZE;
    bufferBarrier.srcAccessMask = vk::AccessFlags::ShaderWrite | vk::AccessFlags::ShaderRead;
    bufferBarrier.dstAccessMask = vk::AccessFlags::HostRead;
    bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::Host, {}, {}, { bufferBarrier }, {});
}

struct Score {
    uint32_t score;
    uint32_t index;
};

void ComputeGenerator::readResult(size_t index, std::vector<glm::ivec2>& openList, std::vector<Color32>& colors) {
    auto& frameData = m_frameData[index];

    Score* readBack = static_cast<Score*>(frameData.outputMapping);
    uint32_t workGroupCount = getWorkGroupCount(openList.size());

    for (uint32_t i = 0; i < colors.size(); i++) {
        uint32_t start = i * getWorkGroupCount(m_size.x * m_size.y);
        uint32_t bestScore = std::numeric_limits<uint32_t>::max();
        uint32_t result = 0;

        for (uint32_t j = 0; j < workGroupCount; j++) {
            Score score = readBack[start + j];
            if (score.score < bestScore) {
                result = score.index;
                bestScore = score.score;
            }
        }

        glm::ivec2 pos = openList[result];
        Color32& existingColor = m_bitmap.getPixel(pos.x, pos.y);
        if (existingColor.a == 0) {
            m_colorQueue->enqueue(pos, colors[i]);
            existingColor = colors[i];
            addNeighborsToOpenSet(pos);
            m_openSet.erase(pos);
            m_queue.push({ colors[i], pos });
        } else {
            m_source->resubmit(colors[i]);
        }
    }
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
            && n.x < m_bitmap.width() && n.y < m_bitmap.height()
            && m_bitmap.getPixel(n.x, n.y).a == 0) {
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

    auto commandBuffers = m_commandPool->allocate(info);

    for (size_t i = 0; i < FRAMES; i++) {
        m_frameData[i].commandBuffer = std::make_unique<vk::CommandBuffer>(std::move(commandBuffers[i]));
    }
}

void ComputeGenerator::createTexture() {
    vk::ImageCreateInfo info = {};
    info.format = vk::Format::R8G8B8A8_Uint;
    info.extent = { static_cast<uint32_t>(m_size.x), static_cast<uint32_t>(m_size.y), 1 };
    info.arrayLayers = 1;
    info.imageType = vk::ImageType::_2D;
    info.initialLayout = vk::ImageLayout::Undefined;
    info.mipLevels = 1;
    info.samples = vk::SampleCountFlags::_1;
    info.usage = vk::ImageUsageFlags::Storage | vk::ImageUsageFlags::TransferDst;

    m_texture = std::make_unique<vk::Image>(m_core->device(), info);

    Allocation alloc = m_allocator->allocate(m_texture->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_texture->bind(*alloc.memory, alloc.offset);

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

void ComputeGenerator::createPositionBuffers() {
    for (size_t i = 0; i < FRAMES; i++) {
        auto& frameData = m_frameData[i];

        vk::BufferCreateInfo info = {};
        info.size = sizeof(glm::ivec2) * m_size.x * m_size.y;
        info.usage = vk::BufferUsageFlags::StorageBuffer;

        frameData.positionBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

        Allocation alloc = m_allocator->allocate(frameData.positionBuffer->requirements(),
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent | vk::MemoryPropertyFlags::DeviceLocal,
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent);
        frameData.positionBuffer->bind(*alloc.memory, alloc.offset);
        frameData.positionMapping = m_allocator->getMapping(alloc.memory, alloc.offset);
    }
}

void ComputeGenerator::createColorBuffers() {
    for (size_t i = 0; i < FRAMES; i++) {
        auto& frameData = m_frameData[i];

        vk::BufferCreateInfo info = {};
        info.size = sizeof(glm::ivec4) * m_maxBatchAbsolute;
        info.usage = vk::BufferUsageFlags::StorageBuffer;

        frameData.colorBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

        Allocation alloc = m_allocator->allocate(frameData.colorBuffer->requirements(),
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent | vk::MemoryPropertyFlags::DeviceLocal,
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent);
        frameData.colorBuffer->bind(*alloc.memory, alloc.offset);
        frameData.colorMapping = m_allocator->getMapping(alloc.memory, alloc.offset);
    }
}

void ComputeGenerator::createOutputBuffers() {
    for (size_t i = 0; i < FRAMES; i++) {
        auto& frameData = m_frameData[i];

        vk::BufferCreateInfo info = {};
        info.size = sizeof(Score) * getWorkGroupCount(m_size.x * m_size.y) * m_maxBatchAbsolute;
        info.usage = vk::BufferUsageFlags::StorageBuffer;

        frameData.outputBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

        Allocation alloc = m_allocator->allocate(frameData.outputBuffer->requirements(),
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent | vk::MemoryPropertyFlags::HostCached,
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent);
        frameData.outputBuffer->bind(*alloc.memory, alloc.offset);
        frameData.outputMapping = m_allocator->getMapping(alloc.memory, alloc.offset);
    }
}

void ComputeGenerator::createDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding binding0 = {};
    binding0.binding = 0;
    binding0.descriptorType = vk::DescriptorType::StorageImage;
    binding0.descriptorCount = 1;
    binding0.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutBinding binding1 = {};
    binding1.binding = 1;
    binding1.descriptorType = vk::DescriptorType::StorageBuffer;
    binding1.descriptorCount = 1;
    binding1.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutBinding binding2 = {};
    binding2.binding = 2;
    binding2.descriptorType = vk::DescriptorType::StorageBuffer;
    binding2.descriptorCount = 1;
    binding2.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutBinding binding3 = {};
    binding3.binding = 3;
    binding3.descriptorType = vk::DescriptorType::StorageBuffer;
    binding3.descriptorCount = 1;
    binding3.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindings = { binding0, binding1, binding2, binding3 };

    m_descriptorSetLayout = std::make_unique<vk::DescriptorSetLayout>(m_core->device(), info);
}

void ComputeGenerator::createDescriptorPool() {
    vk::DescriptorPoolSize size0 = {};
    size0.type = vk::DescriptorType::StorageImage;
    size0.descriptorCount = 1 * FRAMES;

    vk::DescriptorPoolSize size1 = {};
    size1.type = vk::DescriptorType::StorageBuffer;
    size1.descriptorCount = 3 * FRAMES;

    vk::DescriptorPoolCreateInfo info = {};
    info.maxSets = FRAMES;
    info.poolSizes = { size0, size1 };

    m_descriptorPool = std::make_unique<vk::DescriptorPool>(m_core->device(), info);
}

void ComputeGenerator::createDescriptorSets() {
    vk::DescriptorSetAllocateInfo info = {};
    info.descriptorPool = m_descriptorPool.get();
    info.setLayouts = { *m_descriptorSetLayout, *m_descriptorSetLayout };
    
    auto descriptorSets = m_descriptorPool->allocate(info);

    for (size_t i = 0; i < FRAMES; i++) {
        m_frameData[i].descriptor = std::make_unique<vk::DescriptorSet>(std::move(descriptorSets[i]));
    }
}

void ComputeGenerator::writeDescriptors() {
    for (size_t i = 0; i < FRAMES; i++) {
        auto& frameData = m_frameData[i];

        vk::DescriptorImageInfo imageInfo = {};
        imageInfo.imageView = m_textureView.get();
        imageInfo.imageLayout = vk::ImageLayout::General;

        vk::DescriptorBufferInfo bufferInfo0 = {};
        bufferInfo0.buffer = frameData.positionBuffer.get();
        bufferInfo0.range = frameData.positionBuffer->size();

        vk::DescriptorBufferInfo bufferInfo1 = {};
        bufferInfo1.buffer = frameData.colorBuffer.get();
        bufferInfo1.range = frameData.colorBuffer->size();

        vk::DescriptorBufferInfo bufferInfo2 = {};
        bufferInfo2.buffer = frameData.outputBuffer.get();
        bufferInfo2.range = frameData.outputBuffer->size();

        vk::WriteDescriptorSet write0 = {};
        write0.dstSet = frameData.descriptor.get();
        write0.dstBinding = 0;
        write0.imageInfo = { imageInfo };
        write0.descriptorType = vk::DescriptorType::StorageImage;

        vk::WriteDescriptorSet write1 = {};
        write1.dstSet = frameData.descriptor.get();
        write1.dstBinding = 1;
        write1.bufferInfo = { bufferInfo0 };
        write1.descriptorType = vk::DescriptorType::StorageBuffer;

        vk::WriteDescriptorSet write2 = {};
        write2.dstSet = frameData.descriptor.get();
        write2.dstBinding = 2;
        write2.bufferInfo = { bufferInfo1 };
        write2.descriptorType = vk::DescriptorType::StorageBuffer;

        vk::WriteDescriptorSet write3 = {};
        write3.dstSet = frameData.descriptor.get();
        write3.dstBinding = 3;
        write3.bufferInfo = { bufferInfo2 };
        write3.descriptorType = vk::DescriptorType::StorageBuffer;

        vk::DescriptorSet::update(m_core->device(), { write0, write1, write2, write3 }, {});
    }
}

void ComputeGenerator::createUpdatePipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = sizeof(UpdatePushConstants);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { *m_descriptorSetLayout };
    info.pushConstantRanges = { range };

    m_updatePipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void ComputeGenerator::createUpdatePipeline() {
    vk::ShaderModule module = loadShader(m_core->device(), "shaders/update.comp.spv");

    vk::PipelineShaderStageCreateInfo shaderInfo = {};
    shaderInfo.module = &module;
    shaderInfo.name = "main";
    shaderInfo.stage = vk::ShaderStageFlags::Compute;

    vk::ComputePipelineCreateInfo info = {};
    info.stage = shaderInfo;
    info.layout = m_updatePipelineLayout.get();

    m_updatePipeline = std::make_unique<vk::ComputePipeline>(m_core->device(), info);
}

void ComputeGenerator::createMainPipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = sizeof(MainPushConstants);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { *m_descriptorSetLayout };
    info.pushConstantRanges = { range };
    
    m_mainPipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void ComputeGenerator::createMainPipeline(const std::string& shader) {
    vk::ShaderModule module = loadShader(m_core->device(), shader);

    uint32_t specData[] = { m_workGroupSize, getWorkGroupCount(m_size.x * m_size.y) };

    vk::SpecializationMapEntry entry0 = {};
    entry0.constantID = 0;
    entry0.size = sizeof(uint32_t);
    entry0.offset = 0;

    vk::SpecializationMapEntry entry1 = {};
    entry1.constantID = 1;
    entry1.size = sizeof(uint32_t);
    entry1.offset = sizeof(uint32_t);

    vk::SpecializationInfo specInfo = {};
    specInfo.dataSize = sizeof(specData);
    specInfo.data = &specData;
    specInfo.mapEntries = { entry0, entry1 };

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

void ComputeGenerator::createFences() {
    vk::FenceCreateInfo info = {};
    info.flags = vk::FenceCreateFlags::Signaled;

    for (size_t i = 0; i < FRAMES; i++) {
        m_fences.emplace_back(m_core->device(), info);
    }
}

uint32_t ComputeGenerator::getWorkGroupCount(size_t count) {
    return static_cast<uint32_t>(count / m_workGroupSize) + ((count % m_workGroupSize) == 0 ? 0 : 1);
}