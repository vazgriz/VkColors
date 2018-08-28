#include "ComputeGenerator.h"
#include "Utilities.h"
#include <iostream>
#include <cmath>

#define FRAMES 2
#define GROUP_SIZE 64
#define STAGING_SIZE (64 * 1024 * 1024)
#define BATCH_SIZE 1024

uint32_t getWorkGroupCount(size_t count) {
    return static_cast<uint32_t>(count / GROUP_SIZE) + ((count % GROUP_SIZE) == 0 ? 0 : 1);
}

ComputeGenerator::ComputeGenerator(Core& core, Allocator& allocator, ColorSource& source, glm::ivec2 size, ColorQueue& colorQueue, const std::string& shader)
    : m_bitmap(size.x, size.y) {
    m_core = &core;
    m_allocator = &allocator;
    m_source = &source;
    m_size = size;
    m_colorQueue = &colorQueue;

    m_running = std::make_unique<std::atomic_bool>();

    createCommandPool();
    createCommandBuffers();
    createTexture();
    createTextureView();
    createInputBuffers();
    createOutputBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    writeDescriptors();
    createUpdatePipelineLayout();
    createUpdatePipeline();
    createMainPipelineLayout();
    createMainPipeline(shader);
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

        memcpy(m_inputMappings[index], openList.data(), openList.size() * sizeof(glm::ivec2));

        vk::CommandBuffer& commandBuffer = m_commandBuffers[index];
        commandBuffer.reset(vk::CommandBufferResetFlags::None);

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlags::OneTimeSubmit;

        commandBuffer.begin(beginInfo);

        record(commandBuffer, openList, colorList, index);

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
    glm::ivec4 color;
    int32_t count;
    uint32_t batchID;
};

void ComputeGenerator::record(vk::CommandBuffer& commandBuffer, std::vector<glm::ivec2>& openList, std::vector<Color32>& colors, size_t index) {
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
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::Compute, *m_updatePipelineLayout, 0, { m_descriptorSets[index] }, {});

        UpdatePushConstants updateConstants = {};
        updateConstants.color = glm::ivec4{ item.color.r, item.color.g, item.color.b, 255 };
        updateConstants.pos = item.pos;

        commandBuffer.pushConstants(*m_updatePipelineLayout, vk::ShaderStageFlags::Compute, 0, sizeof(UpdatePushConstants), &updateConstants);
        commandBuffer.dispatch(1, 1, 1);
    }

    barrier.srcAccessMask = vk::AccessFlags::ShaderWrite;
    barrier.dstAccessMask = vk::AccessFlags::ShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::ComputeShader, {}, {}, {}, { barrier });

    uint32_t batchSize = std::max<uint32_t>(1, std::min<uint32_t>(BATCH_SIZE, static_cast<uint32_t>(openList.size() / 1024)));

    //main shader
    for (uint32_t i = 0; i < batchSize; i++) {
        if (!m_source->hasNext()) break;
        Color32 color = m_source->getNext();
        color.a = 255;
        colors.push_back(color);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_mainPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::Compute, *m_mainPipelineLayout, 0, { m_descriptorSets[index] }, {});

        MainPushConstants mainConstants = {};
        mainConstants.count = static_cast<uint32_t>(openList.size());
        mainConstants.color = glm::ivec4{ color.r, color.g, color.b, 255 };
        mainConstants.batchID = i;

        commandBuffer.pushConstants(*m_mainPipelineLayout, vk::ShaderStageFlags::Compute, 0, sizeof(MainPushConstants), &mainConstants);

        uint32_t mainGroups = getWorkGroupCount(openList.size());
        commandBuffer.dispatch(mainGroups, 1, 1);
    }

    vk::BufferMemoryBarrier bufferBarrier = {};
    bufferBarrier.buffer = &m_readBuffers[index];
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
    Score* readBack = static_cast<Score*>(m_readMappings[index]);
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

    m_commandBuffers = m_commandPool->allocate(info);
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

void ComputeGenerator::createInputBuffers() {
    for (size_t i = 0; i < FRAMES; i++) {
        vk::BufferCreateInfo info = {};
        info.size = sizeof(glm::ivec2) * m_size.x * m_size.y;
        info.usage = vk::BufferUsageFlags::StorageBuffer | vk::BufferUsageFlags::TransferDst;

        m_inputBuffers.emplace_back(m_core->device(), info);

        Allocation alloc = m_allocator->allocate(m_inputBuffers[i].requirements(),
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent | vk::MemoryPropertyFlags::DeviceLocal,
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent);
        m_inputBuffers[i].bind(*alloc.memory, alloc.offset);
        m_inputMappings.push_back(m_allocator->getMapping(alloc.memory, alloc.offset));
    }
}

void ComputeGenerator::createOutputBuffers() {
    for (size_t i = 0; i < FRAMES; i++) {
        vk::BufferCreateInfo info = {};
        info.size = sizeof(Score) * getWorkGroupCount(m_size.x * m_size.y) * BATCH_SIZE;
        info.usage = vk::BufferUsageFlags::StorageBuffer;

        m_readBuffers.emplace_back(m_core->device(), info);

        Allocation alloc = m_allocator->allocate(m_readBuffers[i].requirements(),
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent | vk::MemoryPropertyFlags::HostCached,
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent);
        m_readBuffers[i].bind(*alloc.memory, alloc.offset);
        m_readMappings.push_back(m_allocator->getMapping(alloc.memory, alloc.offset));
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

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindings = { binding0, binding1, binding2 };

    m_descriptorSetLayout = std::make_unique<vk::DescriptorSetLayout>(m_core->device(), info);
}

void ComputeGenerator::createDescriptorPool() {
    vk::DescriptorPoolSize size0 = {};
    size0.type = vk::DescriptorType::StorageImage;
    size0.descriptorCount = 1 * FRAMES;

    vk::DescriptorPoolSize size1 = {};
    size1.type = vk::DescriptorType::StorageBuffer;
    size1.descriptorCount = 2 * FRAMES;

    vk::DescriptorPoolCreateInfo info = {};
    info.maxSets = FRAMES;
    info.poolSizes = { size0, size1 };

    m_descriptorPool = std::make_unique<vk::DescriptorPool>(m_core->device(), info);
}

void ComputeGenerator::createDescriptorSets() {
    vk::DescriptorSetAllocateInfo info = {};
    info.descriptorPool = m_descriptorPool.get();
    info.setLayouts = { *m_descriptorSetLayout, *m_descriptorSetLayout };
    
    m_descriptorSets = m_descriptorPool->allocate(info);
}

void ComputeGenerator::writeDescriptors() {
    for (size_t i = 0; i < FRAMES; i++) {
        vk::DescriptorImageInfo imageInfo = {};
        imageInfo.imageView = m_textureView.get();
        imageInfo.imageLayout = vk::ImageLayout::General;

        vk::DescriptorBufferInfo bufferInfo0 = {};
        bufferInfo0.buffer = &m_inputBuffers[i];
        bufferInfo0.range = m_inputBuffers[i].size();

        vk::DescriptorBufferInfo bufferInfo1 = {};
        bufferInfo1.buffer = &m_readBuffers[i];
        bufferInfo1.range = m_readBuffers[i].size();

        vk::WriteDescriptorSet write0 = {};
        write0.dstSet = &m_descriptorSets[i];
        write0.dstBinding = 0;
        write0.imageInfo = { imageInfo };
        write0.descriptorType = vk::DescriptorType::StorageImage;

        vk::WriteDescriptorSet write1 = {};
        write1.dstSet = &m_descriptorSets[i];
        write1.dstBinding = 1;
        write1.bufferInfo = { bufferInfo0 };
        write1.descriptorType = vk::DescriptorType::StorageBuffer;

        vk::WriteDescriptorSet write2 = {};
        write2.dstSet = &m_descriptorSets[i];
        write2.dstBinding = 2;
        write2.bufferInfo = { bufferInfo1 };
        write2.descriptorType = vk::DescriptorType::StorageBuffer;

        vk::DescriptorSet::update(m_core->device(), { write0, write1, write2 }, {});
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

    uint32_t specData[] = { GROUP_SIZE, getWorkGroupCount(m_size.x * m_size.y) };

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