#include <iostream>
#include <GLFW/glfw3.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "Core.h"
#include "Allocator.h"
#include "Renderer.h"
#include "ShuffleSource.h"
#include "Generator.h"
#include "Pyramid.h"

int main() {
    auto last = std::chrono::system_clock::now();
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Colors", nullptr, nullptr);

    Core core = Core(window);
    Allocator allocator = Allocator(core);
    Bitmap bitmap = Bitmap(2048, 1024);

    //for (size_t x = 0; x < bitmap.width(); x++) {
    //    for (size_t y = 0; y < bitmap.height(); y++) {
    //        if (((x / 8) + (y / 8)) % 2 == 0) {
    //            bitmap.getPixel(x, y) = { 255, 255, 255, 0 };
    //        }
    //    }
    //}

    Renderer renderer = Renderer(core, allocator, bitmap);
    ShuffleSource source = ShuffleSource(5);
    Pyramid pyramid = Pyramid(core, allocator);
    Generator generator = Generator(core, source, bitmap, pyramid, "shaders/wave.comp.spv");
    generator.run();

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

    generator.stop();
    core.device().waitIdle();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}