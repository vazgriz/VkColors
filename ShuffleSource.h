#include "ColorSource.h"

class ShuffleSource : public ColorSource {
public:
    ShuffleSource(size_t bitDepth);
    ShuffleSource(const ShuffleSource& other) = delete;
    ShuffleSource& operator = (const ShuffleSource& other) = delete;
    ShuffleSource(ShuffleSource&& other);
    ShuffleSource& operator = (ShuffleSource&& other) = default;

    bool hasNext();
    Color32 getNext();

private:
    std::vector<Color32> m_colors;
    size_t m_index = 0;
};