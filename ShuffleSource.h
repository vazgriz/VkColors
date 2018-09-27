#include "ColorSource.h"
#include <queue>
#include "Options.h"

class ShuffleSource : public ColorSource {
public:
    ShuffleSource(const Options& options);
    ShuffleSource(const ShuffleSource& other) = delete;
    ShuffleSource& operator = (const ShuffleSource& other) = delete;
    ShuffleSource(ShuffleSource&& other);
    ShuffleSource& operator = (ShuffleSource&& other) = default;

    bool hasNext();
    Color32 getNext();
    void resubmit(Color32 color);

private:
    std::queue<Color32> m_colors;
};