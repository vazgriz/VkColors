#include "ColorSource.h"

uint8_t ColorSource::map(uint32_t num, uint32_t bitDepth) {
    num = (num + 1) << (8 - bitDepth);
    return static_cast<uint8_t>(num - 1);
}