#pragma once
#include <stdint.h>
#include <vector>

struct Color32 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

class Bitmap {
    Bitmap(size_t width, size_t height);
    Bitmap(const Bitmap& other) = delete;
    Bitmap& operator = (const Bitmap& other) = delete;
    Bitmap(Bitmap&& other);
    Bitmap& operator = (Bitmap&& other) = default;

    size_t width() { return m_width; }
    size_t height() { return m_height; }
    void* data() { return m_data.data(); }

    Color32& getPixel(size_t x, size_t y) { return m_data[x + (y * m_width)]; }

private:
    size_t m_width;
    size_t m_height;
    std::vector<Color32> m_data;
};