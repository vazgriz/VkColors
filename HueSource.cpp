#include "HueSource.h"
#include <math.h>
#include <random>
#include <algorithm>
#include <cmath>

int getHue(Color32 color) {
    int red = color.r;
    int green = color.g;
    int blue = color.b;

    float min = static_cast<float>(std::min(std::min(red, green), blue));
    float max = static_cast<float>(std::max(std::max(red, green), blue));

    if (min == max) {
        return 0;
    }

    float hue = 0.0f;
    if (max == red) {
        hue = (green - blue) / (max - min);

    } else if (max == green) {
        hue = 2.0f + (blue - red) / (max - min);

    } else {
        hue = 4.0f + (red - green) / (max - min);
    }

    hue = hue * 60;
    if (hue < 0) hue = hue + 360;

    return static_cast<int>(round(hue));
}

HueSource::HueSource(const Options& options) {
    std::vector<Color32> colors;
    uint32_t bitDepth = options.bitDepth;

    uint32_t max = static_cast<uint32_t>(pow(2, bitDepth));
    for (uint32_t r = 0; r < max; r++) {
        for (uint32_t g = 0; g < max; g++) {
            for (uint32_t b = 0; b < max; b++) {
                Color32 color = { map(r, bitDepth), map(g, bitDepth), map(b, bitDepth), 255 };
                colors.push_back(color);
            }
        }
    }

    std::sort(colors.begin(), colors.end(),
        [](Color32 a, Color32 b) -> bool {
            return getHue(a) > getHue(b);
        }
    );

    for (Color32 color : colors) {
        m_colors.push_back(color);
    }
}

HueSource::HueSource(HueSource&& other) {
    *this = std::move(other);
}

bool HueSource::hasNext() {
    return m_colors.size() > 0;
}

Color32 HueSource::getNext() {
    auto result = m_colors.front();
    m_colors.pop_front();
    return result;
}

void HueSource::resubmit(Color32 color) {
    m_colors.push_front(color);
}