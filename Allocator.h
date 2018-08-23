#pragma once
#include "Core.h"

#define PAGE_SIZE (128 * 1024 * 1024)

struct Allocation {
    vk::DeviceMemory* memory;
    size_t size;
    size_t offset;
};

struct Page {
    std::unique_ptr<vk::DeviceMemory> memory;
    size_t size;
    size_t ptr;
};

class Allocator {
public:
    Allocator(Core& core);
    Allocator(const Allocator& other) = delete;
    Allocator& operator = (const Allocator& other) = delete;
    Allocator(Allocator&& other);
    Allocator& operator = (Allocator&& other) = default;

    Allocation allocate(vk::MemoryRequirements requirements, vk::MemoryPropertyFlags preferred, vk::MemoryPropertyFlags required);
    void* getMapping(vk::DeviceMemory* memory, size_t offset);

private:
    Core* m_core;
    vk::MemoryProperties m_properties;
    std::vector<std::vector<Page>> m_pages;
    std::unordered_map<vk::DeviceMemory*, void*> m_mappings;

    Allocation tryAlloc(uint32_t type, vk::MemoryRequirements requirements);
    Page* allocNewPage(uint32_t type, size_t size);
};