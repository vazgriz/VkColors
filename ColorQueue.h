#pragma once
#include <mutex>
#include <vector>
#include <glm/glm.hpp>
#include "Bitmap.h"

class ColorQueue {
    struct Item {
        glm::ivec2 pos;
        Color32 color;
    };

public:
    ColorQueue();
    ColorQueue(const ColorQueue& other) = delete;
    ColorQueue& operator = (const ColorQueue& other) = delete;
    ColorQueue(ColorQueue&& other);
    ColorQueue& operator = (ColorQueue&& other) = default;

    void enqueue(glm::ivec2 pos, Color32 color);
    const std::vector<Item>& swap();
    size_t totalCount() const { return m_totalCount; }

private:
    std::unique_ptr<std::mutex> m_mutex;
    std::vector<Item> m_buffers[2];
    std::vector<Item>* m_front;
    std::vector<Item>* m_back;
    size_t m_totalCount;
};