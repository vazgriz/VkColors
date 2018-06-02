#include "Pyramid.h"
#include <math.h>
#include <cmath>

#define ELEMENT_SIZE 8
//struct {
//    uint index;
//    uint score;
//}

Pyramid::Pyramid(Core& core, Allocator& allocator, Bitmap& bitmap) {
    m_core = &core;
    m_allocator = &allocator;
    m_bitmap = &bitmap;

    size_t levels = static_cast<size_t>(ceil(log2(m_bitmap->width() * m_bitmap->height())));

    createDescriptorSetLayout(levels);
    createDescriptorPool(levels);
    createDescriptorSet();
    createBuffers(levels);
    writeDescriptor();
}

Pyramid::Pyramid(Pyramid&& other) {
    *this = std::move(other);
}

void Pyramid::createDescriptorSetLayout(size_t levels) {
    vk::DescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = vk::DescriptorType::StorageBuffer;
    binding.descriptorCount = levels;
    binding.stageFlags = vk::ShaderStageFlags::Compute;

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindings = { binding };

    m_descriptorSetLayout = std::make_unique<vk::DescriptorSetLayout>(m_core->device(), info);
}

void Pyramid::createDescriptorPool(size_t levels) {
    vk::DescriptorPoolSize size = {};
    size.type = vk::DescriptorType::StorageBuffer;
    size.descriptorCount = levels;

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
        if (i == 0) info.usage |= vk::BufferUsageFlags::TransferSrc;
        info.size = static_cast<vk::DeviceSize>(pow(2, i)) * 8;
        
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