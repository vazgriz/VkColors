#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Options {
    bool valid;
    std::string shader;
    glm::ivec2 size;
    int32_t bitDepth;
    uint32_t workGroupSize;
    bool userWorkGroupSize;
    uint32_t maxBatchAbsolute;
    uint32_t maxBatchRelative;
};

Options parseArguments(int argc, char** argv);