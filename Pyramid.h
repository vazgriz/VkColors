#pragma once
#include "Core.h"
#include "Allocator.h"
#include "Bitmap.h"

class Pyramid {
public:
    Pyramid(Core& core, Allocator& allocator, Bitmap& bitmap);
    Pyramid(const Pyramid& other) = delete;
    Pyramid& operator = (const Pyramid& other) = delete;
    Pyramid(Pyramid&& other);
    Pyramid& operator = (Pyramid&& other) = default;

    vk::DescriptorSetLayout& descriptorSetLayout() { return *m_descriptorSetLayout; }
    vk::DescriptorSet& descriptorSet() { return *m_descriptorSet; }
    std::vector<vk::Buffer>& buffers() { return m_buffers; }

private:
    Core* m_core;
    Allocator* m_allocator;
    Bitmap* m_bitmap;
    std::unique_ptr<vk::DescriptorSetLayout> m_descriptorSetLayout;
    std::unique_ptr<vk::DescriptorPool> m_descriptorPool;
    std::unique_ptr<vk::DescriptorSet> m_descriptorSet;
    std::vector<vk::Buffer> m_buffers;

    void createDescriptorSetLayout(size_t levels);
    void createDescriptorPool(size_t levels);
    void createDescriptorSet();
    void createBuffers(size_t levels);
    void writeDescriptor();
};