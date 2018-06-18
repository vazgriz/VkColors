#include "WaveGenerator2.h"

WaveGenerator2::WaveGenerator2(ColorSource& source, Bitmap& bitmap) : m_scratch(bitmap.width(), bitmap.height()) {
    m_source = &source;
    m_bitmap = &bitmap;
    m_running = std::make_unique<std::atomic_bool>();

    glm::ivec2 pos = { static_cast<int>(m_bitmap->width() / 2), static_cast<int>(m_bitmap->height() / 2) };
    if (m_source->hasNext()) {
        m_bitmap->getPixel(pos.x, pos.y) = m_source->getNext();
        addNeighborsToOpenSet(pos);
        updateNeighbors(pos);
    }
}

WaveGenerator2::WaveGenerator2(WaveGenerator2&& other) : m_scratch(std::move(*other.m_bitmap)) {
    *this = std::move(other);
}

void WaveGenerator2::run() {
    *m_running = true;

    m_mainThread = std::thread([this]() -> void { mainLoop(); });
}

void WaveGenerator2::stop() {
    *m_running = false;

    m_mainThread.join();
}

void WaveGenerator2::mainLoop() {
    while (*m_running) {
        if (!m_source->hasNext()) break;
        if (m_openSet.size() == 0) break;

        m_openList.clear();
        for (auto pos : m_openSet) {
            m_openList.push_back(pos);
        }

        m_color = m_source->getNext();

        size_t result = score();
        readResult(result);
    }
}

size_t WaveGenerator2::score() {
    size_t result;
    int32_t bestScore = std::numeric_limits<int32_t>::max();
    for (size_t i = 0; i < m_openList.size(); i++) {
        glm::ivec2 pos = m_openList[i];

        glm::ivec3 testColor = { m_color.r, m_color.g, m_color.b };
        Color32 scratchColor32 = m_scratch.getPixel(pos.x, pos.y);
        glm::ivec3 scratchColor = { scratchColor32.r, scratchColor32.g, scratchColor32.b };

        int32_t score = length2(testColor - scratchColor);
        if (score < bestScore) {
            result = i;
            bestScore = score;
        }
    }

    return result;
}

void WaveGenerator2::readResult(size_t result) {
    glm::ivec2 pos = m_openList[result];
    m_bitmap->getPixel(pos.x, pos.y) = m_color;
    addNeighborsToOpenSet(pos);
    updateNeighbors(pos);
    m_openSet.erase(pos);
}

void WaveGenerator2::addToOpenSet(glm::ivec2 pos) {
    m_openSet.insert(pos);
}

void WaveGenerator2::addNeighborsToOpenSet(glm::ivec2 pos) {
    glm::ivec2 neighbors[8] = {
        pos + glm::ivec2{ -1, -1 },
        pos + glm::ivec2{ -1,  0 },
        pos + glm::ivec2{ -1,  1 },
        pos + glm::ivec2{ 0, -1 },
        pos + glm::ivec2{ 0,  1 },
        pos + glm::ivec2{ 1, -1 },
        pos + glm::ivec2{ 1,  0 },
        pos + glm::ivec2{ 1,  1 },
    };

    for (size_t i = 0; i < 8; i++) {
        auto& n = neighbors[i];
        if (n.x >= 0 && n.y >= 0
            && n.x < m_bitmap->width() && n.y < m_bitmap->height()
            && m_bitmap->getPixel(n.x, n.y).a == 0) {
            addToOpenSet(n);
        }
    }
}

void WaveGenerator2::update(glm::ivec2 pos) {
    glm::ivec2 neighbors[8] = {
        pos + glm::ivec2{ -1, -1 },
        pos + glm::ivec2{ -1,  0 },
        pos + glm::ivec2{ -1,  1 },
        pos + glm::ivec2{ 0, -1 },
        pos + glm::ivec2{ 0,  1 },
        pos + glm::ivec2{ 1, -1 },
        pos + glm::ivec2{ 1,  0 },
        pos + glm::ivec2{ 1,  1 },
    };

    glm::ivec3 sum = {};
    int32_t count = 0;

    for (size_t i = 0; i < 8; i++) {
        auto& n = neighbors[i];
        if (n.x >= 0 && n.y >= 0 && n.x < m_bitmap->width() && n.y < m_bitmap->height()) {
            Color32 color = m_bitmap->getPixel(n.x, n.y);
            if (color.a != 0) {
                sum += glm::ivec3{ color.r, color.g, color.b };
                count++;
            }
        }
    }

    sum /= count;

    m_scratch.getPixel(pos.x, pos.y) = Color32{ static_cast<uint8_t>(sum.r), static_cast<uint8_t>(sum.g), static_cast<uint8_t>(sum.b) };
}

void WaveGenerator2::updateNeighbors(glm::ivec2 pos) {
    glm::ivec2 neighbors[8] = {
        pos + glm::ivec2{ -1, -1 },
        pos + glm::ivec2{ -1,  0 },
        pos + glm::ivec2{ -1,  1 },
        pos + glm::ivec2{ 0, -1 },
        pos + glm::ivec2{ 0,  1 },
        pos + glm::ivec2{ 1, -1 },
        pos + glm::ivec2{ 1,  0 },
        pos + glm::ivec2{ 1,  1 },
    };

    for (size_t i = 0; i < 8; i++) {
        auto& n = neighbors[i];
        if (n.x >= 0 && n.y >= 0 && n.x < m_bitmap->width() && n.y < m_bitmap->height()) {
            update(n);
        }
    }
}