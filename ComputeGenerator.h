#pragma once
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <glm/glm.hpp>
#include "Generator.h"
#include "Core.h"
#include "Allocator.h"
#include "ColorSource.h"
#include "Bitmap.h"
#include "Staging.h"
#include "Utilities.h"
#include "ColorQueue.h"

class ComputeGenerator : public Generator {
    struct ColorPos {
        Color32 color;
        glm::ivec2 pos;
    };

public:
    ComputeGenerator(Core& core, Allocator& allocator, ColorSource& source, glm::ivec2 size, ColorQueue& colorQueue, const std::string& shader);
    ComputeGenerator(const ComputeGenerator& other) = delete;
    ComputeGenerator& operator = (const ComputeGenerator& other) = delete;
    ComputeGenerator(ComputeGenerator&& other);
    ComputeGenerator& operator = (ComputeGenerator&& other) = default;

    void run();
    void stop();

private:
    Core* m_core;
    Allocator* m_allocator;
    ColorSource* m_source;
    ColorQueue* m_colorQueue;
    glm::ivec2 m_size;

    std::vector<Staging> m_stagings;
    Bitmap m_bitmap;
    std::unique_ptr<vk::Image> m_texture;
    std::unique_ptr<vk::ImageView> m_textureView;
    std::unique_ptr<vk::Buffer> m_inputBuffer;
    std::unique_ptr<vk::Buffer> m_readBuffer;
    void* m_resultMapping;
    std::unique_ptr<vk::DescriptorSetLayout> m_descriptorSetLayout;
    std::unique_ptr<vk::DescriptorPool> m_descriptorPool;
    std::unique_ptr<vk::DescriptorSet> m_descriptorSet;
    std::unique_ptr<vk::CommandPool> m_commandPool;
    std::vector<vk::CommandBuffer> m_commandBuffers;
    std::unique_ptr<vk::PipelineLayout> m_mainPipelineLayout;
    std::unique_ptr<vk::Pipeline> m_mainPipeline;
    std::vector<vk::Fence> m_fences;
    size_t m_frame = 0;

    std::unordered_set<glm::ivec2> m_openSet;

    std::thread m_thread;
    std::unique_ptr<std::atomic_bool> m_running;
    std::queue<ColorPos> m_queue;

    void record(vk::CommandBuffer& commandBuffer, std::vector<glm::ivec2>& openList, Color32 color, size_t index);
    void createCommandPool();
    void createCommandBuffers();
    void createTexture();
    void createTextureView();
    void createInputBuffer();
    void createReadBuffer();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSet();
    void writeDescriptors();
    void createMainPipelineLayout();
    void createMainPipeline(const std::string& shader);
    void createFences();

    void addToOpenSet(glm::ivec2 pos);
    void addNeighborsToOpenSet(glm::ivec2 pos);
    void readResult(std::vector<glm::ivec2>& openList, Color32 color);

    void generatorLoop();
};