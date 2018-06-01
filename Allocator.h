#pragma once
#include "Core.h"

#define PAGE_SIZE (128 * 1024 * 1024)

struct Allocation {
    vk::DeviceMemory* memory;
    size_t size;
    size_t offset;
};

struct Page {
    vk::DeviceMemory memory;
    size_t size;
    size_t ptr;
};

struct StagingData {
    size_t offset;
    size_t size;
    vk::Buffer* dstBuffer;
    vk::Image* dstImage;
    vk::ImageLayout imageLayout;
};

class Allocator {
public:
    Allocator(Core& core);
    Allocator(const Allocator& other) = delete;
    Allocator& operator = (const Allocator& other) = delete;
    Allocator(Allocator&& other);
    Allocator& operator = (Allocator&& other) = default;

    Allocation allocate(vk::MemoryRequirements requirements, vk::MemoryPropertyFlags preferred, vk::MemoryPropertyFlags required);

private:
    Core* m_core;
    vk::MemoryProperties m_properties;
    std::vector<std::vector<Page>> m_pages;

    Allocation tryAlloc(uint32_t type, vk::MemoryRequirements requirements);
    Page* allocNewPage(uint32_t type, size_t size);
};