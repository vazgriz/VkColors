#include <iostream>
#include <GLFW/glfw3.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "Core.h"
#include "Allocator.h"
#include "Renderer.h"
#include "ShuffleSource.h"
#include "ComputeGenerator.h"
#include "WaveGenerator.h"
#include "WaveGenerator2.h"
#include "CoralGenerator.h"
#include "ColorQueue.h"

int main() {
    auto last = std::chrono::system_clock::now();
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Colors", nullptr, nullptr);

    glm::ivec2 size = { 512, 512 };
    Core core = Core(window);
    Allocator allocator = Allocator(core);
    ColorQueue colorQueue;
    Renderer renderer = Renderer(core, allocator, size, colorQueue);
    ShuffleSource source = ShuffleSource(6);
    std::unique_ptr<Generator> generator = std::make_unique<ComputeGenerator>(core, allocator, source, size, colorQueue, "shaders/wave.comp.spv");
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