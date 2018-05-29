#include "Bitmap.h"

Bitmap::Bitmap(size_t width, size_t height) {
    m_width = width;
    m_height = height;
    m_data.resize(width * height);
}

Bitmap::Bitmap(Bitmap&& other) {
    *this = std::move(other);
}