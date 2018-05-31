#pragma once
#include "Bitmap.h"

class ColorSource {
public:
    virtual bool hasNext() = 0;
    virtual Color32 getNext() = 0;
};