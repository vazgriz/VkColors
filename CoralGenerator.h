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
#include "Options.h"

class CoralGenerator : public Generator {
public:
    CoralGenerator(ColorSource& source, ColorQueue& colorQueue, Options& options);
    CoralGenerator(const CoralGenerator& other) = delete;
    CoralGenerator& operator = (const CoralGenerator& other) = delete;
    CoralGenerator(CoralGenerator&& other);
    CoralGenerator& operator = (CoralGenerator&& other) = default;

    void run();
    void stop();

private:
    ColorSource * m_source;
    Bitmap m_bitmap;
    ColorQueue* m_queue;
    std::thread m_mainThread;
    std::unique_ptr<std::atomic_bool> m_running;
    std::unordered_set<glm::ivec2> m_openSet;
    std::vector<glm::ivec2> m_openList;
    Color32 m_color;

    void mainLoop();

    void addToOpenSet(glm::ivec2 pos);
    void addNeighborsToOpenSet(glm::ivec2 pos);
    size_t score();
    void readResult(size_t);
};