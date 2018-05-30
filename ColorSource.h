#include "Bitmap.h"

class ColorSource {
public:
    virtual Color32 getNext() = 0;
};