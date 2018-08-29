#pragma once
#include "Generator.h"
#include <vector>
#include <thread>
#include <memory>
#include <atomic>
#include <glm/glm.hpp>
#include <unordered_set>
#include "ColorSource.h"
#include "Bitmap.h"
#include "Utilities.h"
#include "ColorQueue.h"

class WaveGenerator2 : public Generator {
public:
    WaveGenerator2(ColorSource& source, Bitmap& bitmap, ColorQueue& colorQueue);
    WaveGenerator2(const WaveGenerator2& other) = delete;
    WaveGenerator2& operator = (const WaveGenerator2& other) = delete;
    WaveGenerator2(WaveGenerator2&& other);
    WaveGenerator2& operator = (WaveGenerator2&& other) = default;

    void run();
    void stop();

private:
    ColorSource* m_source;
    Bitmap* m_bitmap;
    ColorQueue* m_queue;
    Bitmap m_scratch;
    std::thread m_mainThread;
    std::unique_ptr<std::atomic_bool> m_running;
    std::unordered_set<glm::ivec2> m_openSet;
    std::vector<glm::ivec2> m_openList;
    Color32 m_color;

    void mainLoop();

    void addToOpenSet(glm::ivec2 pos);
    void addNeighborsToOpenSet(glm::ivec2 pos);
    void update(glm::ivec2 pos);
    void updateNeighbors(glm::ivec2 pos);
    size_t score();
    void readResult(size_t);
};