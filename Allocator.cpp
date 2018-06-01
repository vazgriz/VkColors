#include "Allocator.h"
#include "Utilities.h"

Allocator::Allocator(Core& core) {
    m_core = &core;
    m_properties = m_core->device().physicalDevice().memoryProperties();

    m_pages.resize(m_properties.memoryTypes.size());
}

Allocator::Allocator(Allocator&& other) {
    *this = std::move(other);
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