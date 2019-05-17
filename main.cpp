#include <iostream>
#include <GLFW/glfw3.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "Core.h"
#include "Allocator.h"
#include "Renderer.h"
#include "ShuffleSource.h"
#include "HueSource.h"
#include "ComputeGenerator.h"
#include "WaveGenerator.h"
#include "WaveGenerator2.h"
#include "CoralGenerator.h"
#include "ColorQueue.h"
#include "Options.h"

#define AMD_VENDOR_ID 0x1002

int main(int argc, char** argv) {
    auto last = std::chrono::system_clock::now();
    Options options = parseArguments(argc, argv);

    if (!options.valid) {
        return EXIT_FAILURE;
    }

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Colors", nullptr, nullptr);

    Core core = Core(window);

    if (!options.userWorkGroupSize && core.device().physicalDevice().properties().vendorID == AMD_VENDOR_ID) {
        //if work group size not specified and using AMD hardware
        options.workGroupSize = 64;
    }

    Allocator allocator = Allocator(core);
    ColorQueue colorQueue;
    Renderer renderer = Renderer(core, allocator, options.size, colorQueue);
    std::unique_ptr<ColorSource> source;

    if (options.source == Source::Shuffle) {
        source = std::make_unique<ShuffleSource>(options);
    } else if (options.source == Source::Hue) {
        source = std::make_unique<HueSource>(options);
    }

    std::unique_ptr<Generator> generator = std::make_unique<ComputeGenerator>(core, allocator, *source, colorQueue, options);
    generator->run();

    size_t frames = 0;
    size_t lastCount = 0;

    glfwShowWindow(window);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        core.acquire();
        renderer.record(core.getCommandBuffer());
        core.present();

        frames++;
        auto now = std::chrono::system_clock::now();
        std::chrono::duration<float> elapsed = now - last;
        size_t totalCount = colorQueue.totalCount();
        size_t pixelsAdded = totalCount - lastCount;

        if (elapsed.count() > 0.25f) {
            std::stringstream builder;
            builder << std::setprecision(0) << std::fixed;
            builder << "Colors - " << static_cast<double>(frames) / elapsed.count() << " fps " << static_cast<double>(pixelsAdded) / elapsed.count() << " pps";
            glfwSetWindowTitle(window, builder.str().c_str());
            frames = 0;
            last = now;
            lastCount = totalCount;
        }
    }

    generator->stop();
    core.device().waitIdle();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}