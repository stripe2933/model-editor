#include <iostream>

#include "pch.hpp"

#if !defined(__APPLE__) || defined(APPLE_USE_VULKAN)
#define GLFW_INCLUDE_VULKAN
#else
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "App.hpp"
#include "utils/macros.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
#else
int main(int argc, const char *argv[]) {
#endif
#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
    DECLARE_AUTORELEASEPOOL;
#endif

    if (!glfwInit()) {
        fmt::println(std::cerr, "Failed to initialize GLFW.");
        return 1;
    }

    try {
        App{}.run();
    }
    catch (const std::exception &e) {
        fmt::println(std::cerr, "{}", e.what());
        return 1;
    }

    glfwTerminate();

    return 0;
}
