#include "Generator.h"
#include "Utilities.h"
#include <iostream>
#include <cmath>

#define FRAMES 2
#define GROUP_SIZE 64

Generator::Generator(Core& core, Allocator& allocator, ColorSource& source, Bitmap& bitmap, Pyramid& pyramid, const std::string& shader) : m_staging(core, allocator) {
    m_core = &core;
    m_allocator = &allocator;
    m_source = &source;
    m_bitmap = &bitmap;
    m_pyramid = &pyramid;

    m_running = std::make_unique<std::atomic_bool>();

    createCommandPool();
    createCommandBuffers();
    createTexture();
    createTextureView();
    createInputBuffer();
    createOutputBuffer();
    createReadBuffer();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSet();
    writeDescriptors();
    createMainPipelineLayout();
    createMainPipeline(shader);
    createReducePipelineLayout();
    createReducePipeline();
    createFinishPipelineLayout();
    createFinishPipeline();
    createFences();

    glm::ivec2 pos = { static_cast<int>(m_bitmap->width() / 2), static_cast<int>(m_bitmap->height() / 2) };
    if (m_source->hasNext()) {
        m_bitmap->getPixel(pos.x, pos.y) = m_source->getNext();
        addNeighborsToOpenSet(pos);
    }
}

Generator::Generator(Generator&& other) : m_staging(std::move(other.m_staging)) {
    *this = std::move(other);
}

void Generator::run() {
    *m_running = true;
    m_thread = std::thread(&generatorThread, this);
}

void Generator::stop() {
    *m_running = false;
    m_thread.join();
}

void Generator::generatorThread(Generator* generator) {
    generator->generatorLoop();
}

void Generator::generatorLoop() {
    size_t counter = 0;
    std::vector<glm::ivec2> openList;
    Color32 color;

    while (*m_running) {
        openList.clear();
        for (auto& pos : m_openSet) {
            openList.push_back(pos);
        }

        if (openList.size() == 0) break;
        if (!m_source->hasNext()) break;

        color = m_source->getNext();
        color.a = 255;

        size_t index = m_frame % FRAMES;
        m_fences[index].wait();
        m_fences[index].reset();

        if (m_frame > 1) {
            readResult(openList, color);
        }

        vk::CommandBuffer& commandBuffer = m_commandBuffers[index];
        commandBuffer.reset(vk::CommandBufferResetFlags::None);

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlags::OneTimeSubmit;

        commandBuffer.begin(beginInfo);

        m_staging.transfer(openList.data(), openList.size() * sizeof(glm::ivec2), *m_inputBuffer);
        m_staging.flush(commandBuffer);

        record(commandBuffer, openList, color);

        commandBuffer.end();

        m_core->submitCompute(commandBuffer, &m_fences[index]);
        m_frame++;
    }

    vk::Fence::wait(m_core->device(), m_fences, true);
    readResult(openList, color);
}

struct PushConstants {
    int32_t width;
    int32_t height;
    uint32_t count;
    uint32_t targetLevel;
    uint32_t color;
};

void Generator::record(vk::CommandBuffer& commandBuffer, std::vector<glm::ivec2>& openList, Color32 color) {
    if (openList.size() == 0) return;
    commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_mainPipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::Compute, *m_mainPipelineLayout, 0, { m_pyramid->descriptorSet(), *m_descriptorSet }, {});

    PushConstants constants = {};
    constants.width = static_cast<int32_t>(m_bitmap->width());
    constants.height = static_cast<int32_t>(m_bitmap->height());
    constants.count = static_cast<uint32_t>(openList.size());
    constants.targetLevel = static_cast<uint32_t>(ceil(log2(openList.size())));
    memcpy(&constants.color, &color, sizeof(Color32));

    commandBuffer.pushConstants(*m_mainPipelineLayout, vk::ShaderStageFlags::Compute, 0, 5 * sizeof(uint32_t), &constants);

    uint32_t mainGroups = (static_cast<uint32_t>(openList.size()) / GROUP_SIZE) + 1;
    commandBuffer.dispatch(mainGroups, 1, 1);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_reducePipeline);
    commandBuffer.pushConstants(*m_reducePipelineLayout, vk::ShaderStageFlags::Compute, 0, 4 * sizeof(uint32_t), &constants);

    vk::BufferMemoryBarrier barrier = {};
    barrier.buffer = &m_pyramid->buffers()[constants.targetLevel];
    barrier.size = m_pyramid->buffers()[constants.targetLevel].size();
    barrier.dstAccessMask = vk::AccessFlags::ShaderWrite;
    barrier.srcAccessMask = vk::AccessFlags::ShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::ComputeShader, vk::DependencyFlags::None,
        {}, { barrier }, {});

    uint32_t currentCount = constants.count;
    for (uint32_t currentLevel = constants.targetLevel; currentLevel > 0; currentLevel--) {
        currentCount = (currentCount / 2) + (currentCount % 2);
        constants.count = currentCount;
        constants.targetLevel = currentLevel - 1;

        commandBuffer.pushConstants(*m_mainPipelineLayout, vk::ShaderStageFlags::Compute, 0, 4 * sizeof(uint32_t), &constants);

        uint32_t reduceGroups = (static_cast<uint32_t>(openList.size()) / GROUP_SIZE) + 1;
        commandBuffer.dispatch(reduceGroups, 1, 1);

        barrier.buffer = &m_pyramid->buffers()[constants.targetLevel];
        barrier.size = m_pyramid->buffers()[constants.targetLevel].size();
        commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::ComputeShader, vk::DependencyFlags::None,
            {}, { barrier }, {});
    }

    commandBuffer.bindPipeline(vk::PipelineBindPoint::Compute, *m_finishPipeline);
    commandBuffer.pushConstants(*m_finishPipelineLayout, vk::ShaderStageFlags::Compute, 0, 5 * sizeof(uint32_t), &constants);
    commandBuffer.dispatch(1, 1, 1);

    barrier.buffer = m_outputBuffer.get();
    barrier.srcAccessMask = vk::AccessFlags::ShaderWrite;
    barrier.dstAccessMask = vk::AccessFlags::TransferRead;
    barrier.size = m_outputBuffer->size();

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::ComputeShader, vk::PipelineStageFlags::Transfer, vk::DependencyFlags::None,
        {}, { barrier }, {});

    vk::BufferCopy copy = {};
    copy.size = m_readBuffer->size();
    commandBuffer.copyBuffer(*m_outputBuffer, *m_readBuffer, { copy });

    barrier.buffer = m_readBuffer.get();
    barrier.srcAccessMask = vk::AccessFlags::TransferWrite;
    barrier.dstAccessMask = vk::AccessFlags::HostRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlags::Transfer, vk::PipelineStageFlags::Host, vk::DependencyFlags::None,
        {}, { barrier }, {});
}

void Generator::readResult(std::vector<glm::ivec2>& openList, Color32 color) {
    uint32_t result;
    memcpy(&result, m_resultMapping, sizeof(uint32_t));
    glm::ivec2 pos = openList[result];
    m_bitmap->getPixel(pos.x, pos.y) = color;
    addNeighborsToOpenSet(pos);
    m_openSet.erase(pos);
}

void Generator::addToOpenSet(glm::ivec2 pos) {
    m_openSet.insert(pos);
}

void Generator::addNeighborsToOpenSet(glm::ivec2 pos) {
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

void Generator::createCommandPool() {
    vk::CommandPoolCreateInfo info = {};
    info.queueFamilyIndex = m_core->computeQueueFamilyIndex();
    info.flags = vk::CommandPoolCreateFlags::ResetCommandBuffer;

    m_commandPool = std::make_unique<vk::CommandPool>(m_core->device(), info);
}

void Generator::createCommandBuffers() {
    vk::CommandBufferAllocateInfo info = {};
    info.commandPool = m_commandPool.get();
    info.commandBufferCount = FRAMES;

    m_commandBuffers = m_commandPool->allocate(info);
}

void Generator::createTexture() {
    vk::ImageCreateInfo info = {};
    info.format = vk::Format::R8G8B8A8_Unorm;
    info.extent = { static_cast<uint32_t>(m_bitmap->width()), static_cast<uint32_t>(m_bitmap->height()), 1 };
    info.arrayLayers = 1;
    info.imageType = vk::ImageType::_2D;
    info.initialLayout = vk::ImageLayout::Undefined;
    info.mipLevels = 1;
    info.samples = vk::SampleCountFlags::_1;
    info.usage = vk::ImageUsageFlags::Storage;

    m_texture = std::make_unique<vk::Image>(m_core->device(), info);

    Allocation alloc = m_allocator->allocate(m_texture->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_texture->bind(*alloc.memory, alloc.size);
}

void Generator::createTextureView() {
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

void Generator::createInputBuffer() {
    vk::BufferCreateInfo info = {};
    info.size = sizeof(glm::uvec2) * m_bitmap->width() * m_bitmap->height();
    info.usage = vk::BufferUsageFlags::StorageBuffer | vk::BufferUsageFlags::TransferDst;

    m_inputBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

    Allocation alloc = m_allocator->allocate(m_inputBuffer->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_inputBuffer->bind(*alloc.memory, alloc.offset);
}

void Generator::createOutputBuffer() {
    vk::BufferCreateInfo info = {};
    info.size = sizeof(uint32_t);
    info.usage = vk::BufferUsageFlags::StorageBuffer | vk::BufferUsageFlags::TransferSrc;

    m_outputBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);

    Allocation alloc = m_allocator->allocate(m_outputBuffer->requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
    m_outputBuffer->bind(*alloc.memory, alloc.offset);
}

void Generator::createReadBuffer() {
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

void Generator::createDescriptorSetLayout() {
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

    vk::DescriptorSetLayoutBinding binding3 = {};
    binding3.binding = 2;
    binding3.descriptorType = vk::DescriptorType::StorageBuffer;
    binding3.descriptorCount = 1;
    binding3.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindings = { binding1, binding2, binding3 };

    m_descriptorSetLayout = std::make_unique<vk::DescriptorSetLayout>(m_core->device(), info);
}

void Generator::createDescriptorPool() {
    vk::DescriptorPoolSize size1 = {};
    size1.type = vk::DescriptorType::StorageImage;
    size1.descriptorCount = 1;

    vk::DescriptorPoolSize size2 = {};
    size2.type = vk::DescriptorType::StorageBuffer;
    size2.descriptorCount = 2;

    vk::DescriptorPoolCreateInfo info = {};
    info.maxSets = 1;
    info.poolSizes = { size1, size2 };

    m_descriptorPool = std::make_unique<vk::DescriptorPool>(m_core->device(), info);
}

void Generator::createDescriptorSet() {
    vk::DescriptorSetAllocateInfo info = {};
    info.descriptorPool = m_descriptorPool.get();
    info.setLayouts = { *m_descriptorSetLayout };
    
    m_descriptorSet = std::make_unique<vk::DescriptorSet>(std::move(m_descriptorPool->allocate(info)[0]));
}

void Generator::writeDescriptors() {
    vk::DescriptorImageInfo imageInfo = {};
    imageInfo.imageView = m_textureView.get();
    imageInfo.imageLayout = vk::ImageLayout::General;

    vk::DescriptorBufferInfo bufferInfo1 = {};
    bufferInfo1.buffer = m_inputBuffer.get();
    bufferInfo1.range = m_inputBuffer->size();

    vk::DescriptorBufferInfo bufferInfo2 = {};
    bufferInfo2.buffer = m_outputBuffer.get();
    bufferInfo2.range = m_outputBuffer->size();

    vk::WriteDescriptorSet write1 = {};
    write1.dstSet = m_descriptorSet.get();
    write1.dstBinding = 0;
    write1.imageInfo = { imageInfo };
    write1.descriptorType = vk::DescriptorType::StorageImage;

    vk::WriteDescriptorSet write2 = {};
    write2.dstSet = m_descriptorSet.get();
    write2.dstBinding = 1;
    write2.bufferInfo = { bufferInfo1 };
    write2.descriptorType = vk::DescriptorType::StorageBuffer;

    vk::WriteDescriptorSet write3 = {};
    write3.dstSet = m_descriptorSet.get();
    write3.dstBinding = 2;
    write3.bufferInfo = { bufferInfo2 };
    write3.descriptorType = vk::DescriptorType::StorageBuffer;

    vk::DescriptorSet::update(m_core->device(), { write1, write2, write3 }, {});
}

void Generator::createMainPipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = 5 * sizeof(uint32_t);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { m_pyramid->descriptorSetLayout(), *m_descriptorSetLayout };
    info.pushConstantRanges = { range };
    
    m_mainPipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void Generator::createMainPipeline(const std::string& shader) {
    vk::ShaderModule module = loadShader(m_core->device(), shader);

    uint32_t groupSize = GROUP_SIZE;

    vk::SpecializationMapEntry entry = {};
    entry.constantID = 0;
    entry.size = sizeof(groupSize);
    entry.offset = 0;

    vk::SpecializationInfo specInfo = {};
    specInfo.dataSize = sizeof(groupSize);
    specInfo.data = &groupSize;
    specInfo.mapEntries = { entry };

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

void Generator::createReducePipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = 4 * sizeof(uint32_t);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { m_pyramid->descriptorSetLayout() };
    info.pushConstantRanges = { range };

    m_reducePipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void Generator::createReducePipeline() {
    vk::ShaderModule module = loadShader(m_core->device(), "shaders/reduce.comp.spv");

    uint32_t groupSize = GROUP_SIZE;

    vk::SpecializationMapEntry entry = {};
    entry.constantID = 0;
    entry.size = sizeof(groupSize);
    entry.offset = 0;

    vk::SpecializationInfo specInfo = {};
    specInfo.dataSize = sizeof(groupSize);
    specInfo.data = &groupSize;
    specInfo.mapEntries = { entry };

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

void Generator::createFinishPipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = 5 * sizeof(uint32_t);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { m_pyramid->descriptorSetLayout(), *m_descriptorSetLayout };
    info.pushConstantRanges = { range };

    m_finishPipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void Generator::createFinishPipeline() {
    vk::ShaderModule module = loadShader(m_core->device(), "shaders/finish.comp.spv");

    uint32_t groupSize = GROUP_SIZE;

    vk::SpecializationMapEntry entry = {};
    entry.constantID = 0;
    entry.size = sizeof(groupSize);
    entry.offset = 0;

    vk::SpecializationInfo specInfo = {};
    specInfo.dataSize = sizeof(groupSize);
    specInfo.data = &groupSize;
    specInfo.mapEntries = { entry };

    vk::PipelineShaderStageCreateInfo shaderInfo = {};
    shaderInfo.module = &module;
    shaderInfo.name = "main";
    shaderInfo.stage = vk::ShaderStageFlags::Compute;
    shaderInfo.specializationInfo = &specInfo;

    vk::ComputePipelineCreateInfo info = {};
    info.stage = shaderInfo;
    info.layout = m_finishPipelineLayout.get();

    m_finishPipeline = std::make_unique<vk::ComputePipeline>(m_core->device(), info);
}

void Generator::createFences() {
    vk::FenceCreateInfo info = {};
    info.flags = vk::FenceCreateFlags::Signaled;

    for (size_t i = 0; i < FRAMES; i++) {
        m_fences.emplace_back(m_core->device(), info);
    }
}