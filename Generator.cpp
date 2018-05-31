#include "Generator.h"
#include "Utilities.h"
#include <iostream>

#define FRAMES 2
#define GROUP_SIZE 64

Generator::Generator(Core& core, ColorSource& source, Bitmap& bitmap, Pyramid& pyramid, const std::string& shader) {
    m_core = &core;
    m_source = &source;
    m_bitmap = &bitmap;
    m_pyramid = &pyramid;

    m_running = std::make_unique<std::atomic_bool>();

    createPipelineLayout();
    createPipeline(shader);
    createFences();

    if (m_source->hasNext()) {
        m_bitmap->getPixel(m_bitmap->width() / 2, m_bitmap->height() / 2) = m_source->getNext();
    }
}

Generator::Generator(Generator&& other) {
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
    while (*m_running) {

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

    m_commandBuffers = std::move(m_commandPool->allocate(info));
}

void Generator::createPipelineLayout() {
    vk::PushConstantRange range = {};
    range.size = 3 * sizeof(uint32_t);
    range.stageFlags = vk::ShaderStageFlags::Compute;

    vk::PipelineLayoutCreateInfo info = {};
    info.setLayouts = { m_pyramid->descriptorSetLayout() };
    info.pushConstantRanges = { range };
    
    m_pipelineLayout = std::make_unique<vk::PipelineLayout>(m_core->device(), info);
}

void Generator::createPipeline(const std::string& shader) {
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
    info.layout = m_pipelineLayout.get();

    m_pipeline = std::make_unique<vk::ComputePipeline>(m_core->device(), info);
}

void Generator::createFences() {
    vk::FenceCreateInfo info = {};
    info.flags = vk::FenceCreateFlags::Signaled;

    for (size_t i = 0; i < FRAMES; i++) {
        m_fences.emplace_back(m_core->device(), info);
    }
}