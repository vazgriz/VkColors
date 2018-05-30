#include "Allocator.h"

Allocator::Allocator(Core& core) {
    m_core = &core;
    m_properties = m_core->device().physicalDevice().memoryProperties();

    m_pages.resize(m_properties.memoryTypes.size());
    createStagingMemory();
}

Allocator::Allocator(Allocator&& other) {
    *this = std::move(other);
}

size_t Allocator::align(size_t ptr, size_t align) {
    size_t unalign = ptr % align;
    if (unalign != 0) {
        return ptr + (align - unalign);
    } else {
        return ptr;
    }
}

Page* Allocator::allocNewPage(uint32_t type, size_t size) {
    size_t allocSize = 0;
    while (allocSize < size) allocSize += PAGE_SIZE;
    vk::MemoryAllocateInfo info = {};
    info.memoryTypeIndex = type;
    info.allocationSize = allocSize;

    try {
        vk::DeviceMemory memory = vk::DeviceMemory(m_core->device(), info);
        m_pages[type].emplace_back(Page{ std::move(memory), allocSize, 0 });
        return &m_pages[type].back();
    }
    catch (...) {
        return nullptr;
    }
}

Allocation Allocator::tryAlloc(uint32_t type, vk::MemoryRequirements requirements) {
    auto& pages = m_pages[type];

    if (pages.size() > 0) {
        auto& page = pages.back();

        size_t aligned = align(page.ptr, requirements.alignment);
        if (aligned + requirements.size > page.size) {
            return { &page.memory, requirements.size, aligned };
        }
    }

    auto newPage = allocNewPage(type, requirements.size);
    if (newPage != nullptr) {
        return { &newPage->memory, requirements.size, 0 };
    }

    return {};
}

Allocation Allocator::allocate(vk::MemoryRequirements requirements, vk::MemoryPropertyFlags preferred, vk::MemoryPropertyFlags required) {
    for (uint32_t i = 0; i < m_properties.memoryTypes.size(); i++) {
        if (((requirements.memoryTypeBits >> i) & 1) != 0
            && (m_properties.memoryTypes[i].propertyFlags & preferred) == preferred) {
            Allocation result = tryAlloc(i, requirements);
            if (result.memory != nullptr) {
                return result;
            }
        }
    }

    for (uint32_t i = 0; i < m_properties.memoryTypes.size(); i++) {
        if (((requirements.memoryTypeBits >> i) & 1) != 0
            && (m_properties.memoryTypes[i].propertyFlags & required) == required) {
            Allocation result = tryAlloc(i, requirements);
            if (result.memory != nullptr) {
                return result;
            }
        }
    }
    
    return {};
}

void Allocator::createStagingMemory() {
    vk::BufferCreateInfo info = {};
    info.size = PAGE_SIZE;
    info.usage = vk::BufferUsageFlags::TransferSrc;

    m_stagingBuffer = std::make_unique<vk::Buffer>(m_core->device(), info);
    vk::MemoryRequirements requirements = m_stagingBuffer->requirements();
    vk::MemoryPropertyFlags flags = vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent;

    for (uint32_t i = 0; i < m_properties.memoryTypes.size(); i++) {
        if (((requirements.memoryTypeBits >> i) & 1) != 0
            && (m_properties.memoryTypes[i].propertyFlags & flags) == flags) {
            m_stagingMemory = allocNewPage(i, PAGE_SIZE);
            m_stagingMapping = m_stagingMemory->memory.map(0, PAGE_SIZE);
            m_stagingBuffer->bind(m_stagingMemory->memory, 0);
            break;
        }
    }
}

void Allocator::transfer(void* data, size_t size, vk::Buffer& dstBuffer) {
    size_t offset = align(m_stagingMemory->ptr, 4);
    memcpy(static_cast<char*>(m_stagingMapping) + offset, data, size);
    m_stagingData.emplace_back(StagingData{ offset, size, &dstBuffer });
    m_stagingMemory->ptr = offset + size;
}

void Allocator::transfer(void* data, size_t size, vk::Image& dstImage, vk::ImageLayout imageLayout) {
    size_t offset = align(m_stagingMemory->ptr, 4);
    memcpy(static_cast<char*>(m_stagingMapping) + offset, data, size);
    m_stagingData.emplace_back(StagingData{ offset, size, nullptr, &dstImage, imageLayout });
    m_stagingMemory->ptr = offset + size;
}

void Allocator::flushStaging(vk::CommandBuffer& commandBuffer) {
    for (auto& transfer : m_stagingData) {
        if (transfer.dstBuffer != nullptr) {
            vk::BufferCopy copy = {};
            copy.size = transfer.size;
            copy.srcOffset = transfer.offset;

            commandBuffer.copyBuffer(*m_stagingBuffer, *transfer.dstBuffer, copy);
        } else if (transfer.dstImage != nullptr) {
            vk::BufferImageCopy copy = {};
            copy.imageExtent = transfer.dstImage->extent();
            copy.imageSubresource.aspectMask = vk::ImageAspectFlags::Color;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageSubresource.mipLevel = 0;

            commandBuffer.copyBufferToImage(*m_stagingBuffer, *transfer.dstImage, transfer.imageLayout, { copy });
        }
    }

    m_stagingData.clear();
    m_stagingMemory->ptr = 0;
}