#include "ColorQueue.h"

ColorQueue::ColorQueue() {
    m_mutex = std::make_unique<std::mutex>();
    m_front = &m_buffers[0];
    m_back = &m_buffers[1];
}

ColorQueue::ColorQueue(ColorQueue&& other) {
    *this = std::move(other);
}

void ColorQueue::enqueue(glm::ivec2 pos, Color32 color) {
    std::lock_guard<std::mutex> lock(*m_mutex);
    m_front->push_back({ pos, color });
}

const std::vector<ColorQueue::Item>& ColorQueue::swap() {
    std::lock_guard<std::mutex> lock(*m_mutex);
    std::swap(m_front, m_back);
    m_front->clear();
    return *m_back;
}