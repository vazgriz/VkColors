#pragma once
#include "ColorSource.h"
#include <deque>
#include "Options.h"

class HueSource : public ColorSource {
public:
    HueSource(const Options& options);
    HueSource(const HueSource& other) = delete;
    HueSource& operator = (const HueSource& other) = delete;
    HueSource(HueSource&& other);
    HueSource& operator = (HueSource&& other) = default;

    bool hasNext();
    Color32 getNext();
    void resubmit(Color32 color);

private:
    std::deque<Color32> m_colors;
};