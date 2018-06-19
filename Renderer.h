#pragma once
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "Core.h"
#include "Allocator.h"
#include "Bitmap.h"
#include "Staging.h"
#include "ColorQueue.h"

class Renderer : public Observer {
public:
    Renderer(Core& core, Allocator& allocator, Bitmap& bitmap, ColorQueue& colorQueue);
    Renderer(const Renderer& other) = delete;
    Renderer& operator = (const Renderer& other) = delete;
    Renderer(Renderer&& other);
    Renderer& operator = (Renderer&& other) = default;

    void record(vk::CommandBuffer& commandBuffer);

    void onResize(int width, int height);

private:
    Core* m_core;
    Allocator* m_allocator;
    Bitmap* m_bitmap;
    ColorQueue* m_queue;
    Staging m_staging;
    int32_t m_width;
    int32_t m_height;
    std::unique_ptr<vk::Buffer> m_vertexBuffer;
    Allocation m_vertexAlloc;
    std::unique_ptr<vk::Buffer> m_indexBuffer;
    Allocation m_indexAlloc;
    std::unique_ptr<vk::Image> m_texture;
    Allocation m_textureAlloc;
    std::unique_ptr<vk::ImageView> m_textureView;
    std::unique_ptr<vk::Sampler> m_sampler;
    std::unique_ptr<vk::DescriptorSetLayout> m_descriptorLayout;
    std::unique_ptr<vk::DescriptorPool> m_descriptorPool;
    std::unique_ptr<vk::DescriptorSet> m_descriptorSet;
    std::unique_ptr<vk::PipelineLayout> m_pipelineLayout;
    std::unique_ptr<vk::Pipeline> m_pipeline;
    glm::mat4 m_projectionMatrix;

    void createVertexBuffer(vk::CommandBuffer& commandBuffer);
    void createIndexBuffer(vk::CommandBuffer& commandBuffer);
    void createTexture(vk::CommandBuffer& commandBuffer);
    void createTextureView();
    void createSampler();
    void createDescriptorLayout();
    void createDescriptorPool();
    void createDescriptorSet();
    void createPipelineLayout();
    void createPipeline();
};