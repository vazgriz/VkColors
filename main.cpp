#include <iostream>
#include <GLFW/glfw3.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "Core.h"
#include "Allocator.h"
#include "Renderer.h"
#include "ShuffleSource.h"

int main() {
    auto last = std::chrono::system_clock::now();
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Colors", nullptr, nullptr);

    Core core = Core(window);
    Allocator allocator = Allocator(core);
    Renderer renderer = Renderer(core, allocator, 2048, 1024);
    ShuffleSource source = ShuffleSource(5);

    size_t frames = 0;

    glfwShowWindow(window);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        core.acquire();
        renderer.record(core.getCommandBuffer());
        core.present();

        frames++;
        auto now = std::chrono::system_clock::now();
        std::chrono::duration<float> elapsed = now - last;
        if (elapsed.count() > 0.25f) {
            std::stringstream builder;
            builder << "Colors (" << std::setprecision(3) << static_cast<double>(frames) / elapsed.count() << " fps)";
            glfwSetWindowTitle(window, builder.str().c_str());
            frames = 0;
            last = now;
        }
    }

    core.device().waitIdle();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}