#pragma once
#include <string>
#include <vector>

struct Options {
    bool valid;
    std::string shader;
};

Options parseArguments(int argc, char** argv);