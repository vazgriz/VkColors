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
    void transfer(void* data, size_t size, vk::Buffer& dstBuffer);
    void transfer(void* data, size_t size, vk::Image& dstImage, vk::ImageLayout imageLayout);
    void flushStaging(vk::CommandBuffer& commandBuffer);

private:
    Core* m_core;
    vk::MemoryProperties m_properties;
    std::vector<std::vector<Page>> m_pages;
    std::unique_ptr<vk::Buffer> m_stagingBuffer;
    Page* m_stagingMemory;
    void* m_stagingMapping;
    std::vector<StagingData> m_stagingData;

    size_t align(size_t ptr, size_t align);
    Allocation tryAlloc(uint32_t type, vk::MemoryRequirements requirements);
    Page* allocNewPage(uint32_t type, size_t size);
    void createStagingMemory();
};