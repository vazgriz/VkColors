#include "Pyramid.h"
#include <math.h>

#define ELEMENT_SIZE 8
//struct {
//    uint index;
//    uint score;
//}

Pyramid::Pyramid(Core& core, Allocator& allocator) {
    m_core = &core;
    m_allocator = &allocator;

    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSet();
    createBuffers(24);
    writeDescriptor();
}

Pyramid::Pyramid(Pyramid&& other) {
    *this = std::move(other);
}

void Pyramid::createDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = vk::DescriptorType::StorageBuffer;
    binding.descriptorCount = 24;
    binding.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindings = { binding };

    m_descriptorSetLayout = std::make_unique<vk::DescriptorSetLayout>(m_core->device(), info);
}

void Pyramid::createDescriptorPool() {
    vk::DescriptorPoolSize size = {};
    size.type = vk::DescriptorType::StorageBuffer;
    size.descriptorCount = 24;

    vk::DescriptorPoolCreateInfo info = {};
    info.poolSizes = { size };
    info.maxSets = 1;

    m_descriptorPool = std::make_unique<vk::DescriptorPool>(m_core->device(), info);
}

void Pyramid::createDescriptorSet() {
    vk::DescriptorSetAllocateInfo info = {};
    info.descriptorPool = m_descriptorPool.get();
    info.setLayouts = { *m_descriptorSetLayout };

    m_descriptorSet = std::make_unique<vk::DescriptorSet>(std::move(m_descriptorPool->allocate(info)[0]));
}

void Pyramid::createBuffers(size_t levels) {
    for (size_t i = 0; i < levels; i++) {
        vk::BufferCreateInfo info = {};
        info.usage = vk::BufferUsageFlags::StorageBuffer;
        info.size = static_cast<vk::DeviceSize>(pow(2, i + 1)) * 8;
        
        vk::Buffer buffer = vk::Buffer(m_core->device(), info);
        Allocation alloc = m_allocator->allocate(buffer.requirements(), vk::MemoryPropertyFlags::DeviceLocal, vk::MemoryPropertyFlags::DeviceLocal);
        buffer.bind(*alloc.memory, alloc.offset);

        m_buffers.emplace_back(std::move(buffer));
    }
}

void Pyramid::writeDescriptor() {
    std::vector<vk::DescriptorBufferInfo> infos(m_buffers.size());
    for (size_t i = 0; i < infos.size(); i++) {
        auto& info = infos[i];
        info.buffer = &m_buffers[i];
        info.range = m_buffers[i].size();
    }

    vk::WriteDescriptorSet write = {};
    write.dstSet = m_descriptorSet.get();
    write.dstBinding = 0;
    write.bufferInfo = std::move(infos);
    write.descriptorType = vk::DescriptorType::StorageBuffer;

    vk::DescriptorSet::update(m_core->device(), { write }, {});
}