#pragma once
#include "Allocator.h"

struct StagingData {
    size_t offset;
    size_t size;
    vk::Buffer* dstBuffer;
    vk::Image* dstImage;
    vk::ImageLayout imageLayout;
    vk::Extent3D imageExtent;
    vk::Offset3D imageOffset;
};

class Staging {
public:
    Staging(Core& core, Allocator& allocator, size_t size);
    Staging(const Staging& other) = delete;
    Staging& operator = (const Staging& other) = delete;
    Staging(Staging&& other);
    Staging& operator = (Staging&& other) = default;

    void transfer(void* data, size_t size, vk::Buffer& dstBuffer);
    void transfer(void* data, size_t size, vk::Image& dstImage, vk::ImageLayout imageLayout);
    void transfer(void* data, size_t size, vk::Image& dstImage, vk::ImageLayout imageLayout, vk::Extent3D extent, vk::Offset3D offset);
    void flush(vk::CommandBuffer& commandBuffer);

private:
    Core* m_core;
    Allocator* m_allocator;
    Allocation m_alloc;
    size_t ptr = 0;
    std::unique_ptr<vk::Buffer> m_buffer;
    void* m_mapping;
    std::vector<StagingData> m_data;

    void createStagingMemory(size_t size);
};