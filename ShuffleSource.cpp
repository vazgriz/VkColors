#include "ShuffleSource.h"
#include <math.h>
#include <random>

uint8_t map(size_t num, size_t bitDepth) {
    num = (num + 1) << (8 - bitDepth);
    return static_cast<uint8_t>(num - 1);
}

ShuffleSource::ShuffleSource(size_t bitDepth) {
    size_t max = static_cast<size_t>(pow(2, bitDepth));
    for (size_t r = 0; r < max; r++) {
        for (size_t g = 0; g < max; g++) {
            for (size_t b = 0; b < max; b++) {
                Color32 color = { map(r, bitDepth), map(g, bitDepth), map(b, bitDepth) };
                m_colors.push_back(color);
            }
        }
    }

    //fisher yates shuffle
    std::default_random_engine random;

    for (size_t i = m_colors.size() - 1; i >= 1; i--) {
        std::uniform_int_distribution<size_t> dist(0, i);
        size_t j = dist(random);
        std::swap(m_colors[i], m_colors[j]);
    }
}

ShuffleSource::ShuffleSource(ShuffleSource&& other) {
    *this = std::move(other);
}

bool ShuffleSource::hasNext() {
    return m_index < m_colors.size();
}

Color32 ShuffleSource::getNext() {
    auto result = m_colors[m_index];
    m_index++;
    return result;
}