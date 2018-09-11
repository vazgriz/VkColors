#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Options {
    bool valid;
    std::string shader;
    glm::ivec2 size;
};

Options parseArguments(int argc, char** argv);