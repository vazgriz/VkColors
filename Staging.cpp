#include "Staging.h"
#include "Utilities.h"

Staging::Staging(Core& core, Allocator& allocator) {
    m_core = &core;
    m_allocator = &allocator;

    createStagingMemory();
}

Staging::Staging(Staging&& other) {
    *this = std::move(other);
}

void Staging::createStagingMemory() {
    vk::BufferCreateInfo info = {};
    info.size = PAGE_SIZE;
    info.usage = vk::BufferUsageFlags::TransferSrc;

    m_buffer = std::make_unique<vk::Buffer>(m_core->device(), info);
    vk::MemoryRequirements requirements = m_buffer->requirements();

    m_alloc = m_allocator->allocate(m_buffer->requirements(), vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent, vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent);
    m_buffer->bind(*m_alloc.memory, m_alloc.offset);
    m_mapping = m_alloc.memory->map(m_alloc.offset, PAGE_SIZE);
}

void Staging::transfer(void* data, size_t size, vk::Buffer& dstBuffer) {
    size_t offset = align(ptr, 4);
    memcpy(static_cast<char*>(m_mapping) + offset, data, size);
    m_data.emplace_back(StagingData{ offset, size, &dstBuffer });
    ptr = offset + size;
}

void Staging::transfer(void* data, size_t size, vk::Image& dstImage, vk::ImageLayout imageLayout) {
    transfer(data, size, dstImage, imageLayout, dstImage.extent(), {});
}

void Staging::transfer(void* data, size_t size, vk::Image& dstImage, vk::ImageLayout imageLayout, vk::Extent3D extent, vk::Offset3D offset) {
    size_t bufferOffset = align(ptr, 4);
    memcpy(static_cast<char*>(m_mapping) + bufferOffset, data, size);
    m_data.emplace_back(StagingData{ bufferOffset, size, nullptr, &dstImage, imageLayout, extent, offset });
    ptr = bufferOffset + size;
}

void Staging::flush(vk::CommandBuffer& commandBuffer) {
    for (auto& transfer : m_data) {
        if (transfer.dstBuffer != nullptr) {
            vk::BufferCopy copy = {};
            copy.size = transfer.size;
            copy.srcOffset = transfer.offset;

            commandBuffer.copyBuffer(*m_buffer, *transfer.dstBuffer, copy);
        } else if (transfer.dstImage != nullptr) {
            vk::BufferImageCopy copy = {};
            copy.bufferOffset = transfer.offset;
            copy.imageOffset = transfer.imageOffset;
            copy.imageExtent = transfer.imageExtent;
            copy.imageSubresource.aspectMask = vk::ImageAspectFlags::Color;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageSubresource.mipLevel = 0;

            commandBuffer.copyBufferToImage(*m_buffer, *transfer.dstImage, transfer.imageLayout, { copy });
        }
    }

    m_data.clear();
    ptr = 0;
}