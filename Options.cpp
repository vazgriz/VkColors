#include "Options.h"
#include <iostream>

struct Argument {
    bool valid;
    std::string name;
    std::string value;
};

Argument parseArgument(const std::string& arg) {
    Argument argument = { true };

    size_t start = 0;
    if (arg.size() >= 1 && arg[0] == '-') {
        if (arg.size() >= 2 && arg[1] == '-') {
            start = 2;
        } else {
            start = 1;
        }
    }

    if (start == 0) {
        std::cout << "Error: Could not parse " << arg << ". Start arguments with '-' or '--'";
        argument.valid = false;
    }

    size_t equals = arg.find('=');

    if (equals == std::string::npos) {
        argument.name = arg.substr(start);
    } else {
        argument.name = arg.substr(start, equals - start);
        argument.value = arg.substr(equals + 1);
    }

    return argument;
}

void argumentError(Options& options, const std::string& message) {
    options.valid = false;
    std::cout << "Error: " << message << "\n";
}

Options parseArguments(int argc, char** argv) {
    Options options = { true };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        Argument argument = parseArgument(arg);

        if (!argument.valid) {
            options.valid = false;
            continue;
        } else if (argument.name == "shader") {
            if (argument.value.empty()) {
                argumentError( options, "Must specify shader to use");
            } else if (argument.value == "wave" || argument.value == "coral") {
                options.shader = "shaders/" + argument.value + ".comp.spv";
            } else {
                argumentError(options, "Shader must be 'wave' or 'coral'");
            }
        } else {
            std::cout << "Error: Could not parse argument '" << argument.name << "'\n";
            options.valid = false;
        }
    }

    return options;
}