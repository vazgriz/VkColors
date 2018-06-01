#pragma once
#include "Allocator.h"

class Staging {
public:
    Staging(Core& core, Allocator& allocator);
    Staging(const Staging& other) = delete;
    Staging& operator = (const Staging& other) = delete;
    Staging(Staging&& other);
    Staging& operator = (Staging&& other) = default;

    void transfer(void* data, size_t size, vk::Buffer& dstBuffer);
    void transfer(void* data, size_t size, vk::Image& dstImage, vk::ImageLayout imageLayout);
    void flush(vk::CommandBuffer& commandBuffer);

private:
    Core* m_core;
    Allocator* m_allocator;
    Allocation m_alloc;
    size_t ptr = 0;
    std::unique_ptr<vk::Buffer> m_buffer;
    void* m_mapping;
    std::vector<StagingData> m_data;

    void createStagingMemory();
};