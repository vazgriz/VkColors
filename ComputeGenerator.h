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

    struct FrameData {
        std::unique_ptr<vk::Buffer> positionBuffer;
        void* positionMapping;
        std::unique_ptr<vk::Buffer> colorBuffer;
        void* colorMapping;
        std::unique_ptr<vk::Buffer> outputBuffer;
        void* outputMapping;
        std::unique_ptr<vk::DescriptorSet> descriptor;
        std::unique_ptr<vk::CommandBuffer> commandBuffer;
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

    Bitmap m_bitmap;
    std::unique_ptr<vk::Image> m_texture;
    std::unique_ptr<vk::ImageView> m_textureView;
    std::vector<FrameData> m_frameData;
    std::unique_ptr<vk::DescriptorSetLayout> m_descriptorSetLayout;
    std::unique_ptr<vk::DescriptorPool> m_descriptorPool;
    std::unique_ptr<vk::CommandPool> m_commandPool;
    std::unique_ptr<vk::PipelineLayout> m_updatePipelineLayout;
    std::unique_ptr<vk::Pipeline> m_updatePipeline;
    std::unique_ptr<vk::PipelineLayout> m_mainPipelineLayout;
    std::unique_ptr<vk::Pipeline> m_mainPipeline;
    std::vector<vk::Fence> m_fences;
    size_t m_frame = 0;

    std::unordered_set<glm::ivec2> m_openSet;

    std::thread m_thread;
    std::unique_ptr<std::atomic_bool> m_running;
    std::queue<ColorPos> m_queue;

    void record(vk::CommandBuffer& commandBuffer, std::vector<glm::ivec2>& openList, std::vector<Color32>& colors, size_t index, uint32_t batchSize);
    void createCommandPool();
    void createCommandBuffers();
    void createTexture();
    void createTextureView();
    void createPositionBuffers();
    void createColorBuffers();
    void createOutputBuffers();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void writeDescriptors();
    void createUpdatePipelineLayout();
    void createUpdatePipeline();
    void createMainPipelineLayout();
    void createMainPipeline(const std::string& shader);
    void createFences();

    void addToOpenSet(glm::ivec2 pos);
    void addNeighborsToOpenSet(glm::ivec2 pos);
    void readResult(size_t index, std::vector<glm::ivec2>& openList, std::vector<Color32>& colors);

    void generatorLoop();
};