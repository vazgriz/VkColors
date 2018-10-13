#pragma once
#include "Bitmap.h"

class ColorSource {
public:
    virtual bool hasNext() = 0;
    virtual Color32 getNext() = 0;
    virtual void resubmit(Color32 color) = 0;

    static uint8_t map(uint32_t num, uint32_t bitDepth);
};
