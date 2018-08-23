#include "ShuffleSource.h"
#include <math.h>
#include <random>

uint8_t map(size_t num, size_t bitDepth) {
    num = (num + 1) << (8 - bitDepth);
    return static_cast<uint8_t>(num - 1);
}

ShuffleSource::ShuffleSource(size_t bitDepth) {
    std::vector<Color32> colors;

    size_t max = static_cast<size_t>(pow(2, bitDepth));
    for (size_t r = 0; r < max; r++) {
        for (size_t g = 0; g < max; g++) {
            for (size_t b = 0; b < max; b++) {
                Color32 color = { map(r, bitDepth), map(g, bitDepth), map(b, bitDepth), 255 };
                colors.push_back(color);
            }
        }
    }

    //fisher yates shuffle
    std::default_random_engine random;

    for (size_t i = colors.size() - 1; i >= 1; i--) {
        std::uniform_int_distribution<size_t> dist(0, i);
        size_t j = dist(random);
        std::swap(colors[i], colors[j]);
    }

    for (Color32 color : colors) {
        m_colors.push(color);
    }
}

ShuffleSource::ShuffleSource(ShuffleSource&& other) {
    *this = std::move(other);
}

bool ShuffleSource::hasNext() {
    return m_colors.size() > 0;
}

Color32 ShuffleSource::getNext() {
    auto result = m_colors.front();
    m_colors.pop();
    return result;
}

void ShuffleSource::resubmit(Color32 color) {
    m_colors.push(color);
}