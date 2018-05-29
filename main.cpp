#include <iostream>
#include <GLFW/glfw3.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "Core.h"

int main() {
    auto last = std::chrono::system_clock::now();
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Colors", nullptr, nullptr);

    Core core = Core(window, 800, 600);

    size_t frames = 0;

    glfwShowWindow(window);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        core.acquire();
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

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}