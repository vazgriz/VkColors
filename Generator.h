#pragma once
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include "Core.h"
#include "ColorSource.h"
#include "Bitmap.h"
#include "Pyramid.h"
#include <atomic>
#include "Staging.h"

class Generator {
public:
    Generator(Core& core, Allocator& allocator, ColorSource& source, Bitmap& bitmap, Pyramid& pyramid, const std::string& shader);
    Generator(const Generator& other) = delete;
    Generator& operator = (const Generator& other) = delete;
    Generator(Generator&& other);
    Generator& operator = (Generator&& other) = default;

    void run();
    void stop();

private:
    Core* m_core;
    Allocator* m_allocator;
    ColorSource* m_source;
    Bitmap* m_bitmap;
    Pyramid* m_pyramid;
    Staging m_staging;
    std::unique_ptr<vk::CommandPool> m_commandPool;
    std::vector<vk::CommandBuffer> m_commandBuffers;
    std::unique_ptr<vk::PipelineLayout> m_pipelineLayout;
    std::unique_ptr<vk::Pipeline> m_pipeline;
    std::vector<vk::Fence> m_fences;
    size_t m_index = 0;
    std::thread m_thread;
    std::unique_ptr<std::atomic_bool> m_running;

    void createCommandPool();
    void createCommandBuffers();
    void createPipelineLayout();
    void createPipeline(const std::string& shader);
    void createFences();
    static void generatorThread(Generator* generator);
    void generatorLoop();
};