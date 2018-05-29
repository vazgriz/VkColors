#include <iostream>
#include <GLFW/glfw3.h>
#include "Core.h"

int main() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Colors", nullptr, nullptr);

    Core core = Core(window, 800, 600);

    glfwShowWindow(window);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        core.acquire();
        core.present();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}