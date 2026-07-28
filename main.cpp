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
#include <shellapi.h>
#endif

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    int argc;
    const LPWSTR* const argv = CommandLineToArgvW(pCmdLine, &argc);
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
        App app;

        // Load assets specified in the command line.
        for (int i = 1; i < argc; ++i) {
            app.loadAsset(argv[i]);
        }

        app.run();
    }
    catch (const std::exception &e) {
        fmt::println(std::cerr, "{}", e.what());
        return 1;
    }

    glfwTerminate();

    return 0;
}
