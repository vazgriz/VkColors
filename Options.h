#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

enum class Source {
    Shuffle,
    Hue
};

enum class GeneratorType {
    Shader,
    CPUWave,
    CPUCoral,
};

struct Options {
    bool valid;
    GeneratorType generator;
    std::string shader;
    glm::ivec2 size;
    int32_t bitDepth;
    uint32_t workGroupSize;
    bool userWorkGroupSize;
    uint32_t maxBatchAbsolute;
    uint32_t maxBatchRelative;
    uint32_t seed;
    Source source;
};

Options parseArguments(int argc, char** argv);