#include "Options.h"
#include <iostream>
#include <chrono>
#include <cctype>

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
        std::cout << "Error: Could not parse '" << arg << "'. Start arguments with '-' or '--'";
        argument.valid = false;
    }

    size_t equals = arg.find('=');

    if (equals == std::string::npos) {
        argument.name = arg.substr(start);
    } else {
        argument.name = arg.substr(start, equals - start);
        argument.value = arg.substr(equals + 1);
    }

    for (char& c : argument.name) {
        c = std::tolower(c);
    }

    for (char& c : argument.value) {
        c = std::tolower(c);
    }

    return argument;
}

void argumentError(Options& options, const std::string& message) {
    options.valid = false;
    std::cout << "Error: " << message << "\n";
}

void parseSize(Options& options, const std::string& size) {
    size_t x = size.find("x");

    if (x == std::string::npos) {
        argumentError(options, "Size must have two dimensions, eg '512x512'");
        return;
    }

    std::string widthString = size.substr(0, x);
    std::string heightString = size.substr(x + 1);

    int32_t width;
    int32_t height;

    try {
        width = std::stoi(widthString);
        height = std::stoi(heightString);
    }
    catch (...) {
        argumentError(options, "Unable to parse size");
        return;
    }

    if (width < 0 || height < 0) {
        argumentError(options, "Width and height must be positive");
        return;
    }

    if (width > 4096 || height > 4096) {
        argumentError(options, "Width and height must be be less than or equal to 4096");
        return;
    }

    options.size = { width, height };
}

int32_t getBitDepth(glm::ivec2 size) {
    int32_t area = size.x * size.y;
    double bitDepth = std::ceil(std::log2(area) / 3);
    return static_cast<int32_t>(bitDepth);
}

Options parseArguments(int argc, char** argv) {
    Options options = {
        true,
        GeneratorType::Shader,
        "shaders/coral.comp.spv",
        { 512, 512 },
        6,
        32, false,
        64,
        1024,
        static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count()),
        Source::Shuffle
    };

    bool userDepth = false;
    bool userMaxBatchAbsolute = false;
    bool userMaxBatchRelative = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        Argument argument = parseArgument(arg);

        if (!argument.valid) {
            options.valid = false;
            continue;
        } else if (argument.name == "generator") {
            if (argument.value.empty()) {
                argumentError(options, "Must specify generator to use");
            } else if (argument.value == "wave" || argument.value == "coral") {
                options.generator = GeneratorType::Shader;
                options.shader = "shaders/" + argument.value + ".comp.spv";

                if (!userMaxBatchRelative) {
                    if (argument.value == "wave") {
                        options.maxBatchRelative = 32;
                    } else {
                        options.maxBatchRelative = 1024;
                    }
                }
            } else if (argument.value == "cpu-wave") {
                options.generator = GeneratorType::CPUWave;
            } else if (argument.value == "cpu-coral") {
                options.generator = GeneratorType::CPUCoral;
            } else {
                argumentError(options, "Shader must be 'wave', 'coral', 'cpu-wave', or 'cpu-coral'");
            }
        } else if (argument.name == "size") {
            parseSize(options, argument.value);

            if (!userDepth) {
                options.bitDepth = getBitDepth(options.size);
            }
        } else if (argument.name == "bitdepth") {
            try {
                options.bitDepth = std::stoi(argument.value);
                userDepth = true;
            }
            catch (...) {
                argumentError(options, "Unable to parse bit depth");
            }

            if (options.bitDepth < 0) {
                argumentError(options, "Bit depth must be positive");
            }

            if (options.bitDepth > 8) {
                argumentError(options, "Bit depth must be less than or equal to 8");
            }
        } else if (argument.name == "workgroupsize") {
            try {
                options.workGroupSize = std::stoul(argument.value);
                options.userWorkGroupSize = true;
            }
            catch (...) {
                argumentError(options, "Unable to parse work group size");
            }
        } else if (argument.name == "maxbatchabsolute") {
            try {
                options.maxBatchAbsolute = std::stoul(argument.value);
                userMaxBatchAbsolute = true;
            }
            catch (...) {
                argumentError(options, "Unable to parse max batch absolute");
            }
        } else if (argument.name == "maxbatchrelative") {
            try {
                options.maxBatchRelative = std::stoul(argument.value);
                userMaxBatchRelative = true;
            }
            catch (...) {
                argumentError(options, "Unable to parse max batch relative");
            }
        } else if (argument.name == "seed") {
            try {
                options.seed = std::stoul(argument.value);
            }
            catch (...) {
                argumentError(options, "Unable to parse max batch relative");
            }
        } else if (argument.name == "color") {
            if (argument.value == "shuffle") {
                options.source = Source::Shuffle;
            } else if (argument.value == "hue") {
                options.source = Source::Hue;
            } else {
                argumentError(options, "Unable to parse color source");
            }
        } else {
            std::cout << "Error: Could not parse argument '" << argument.name << "'\n";
            options.valid = false;
        }
    }

    return options;
}