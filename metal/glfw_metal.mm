#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <QuartzCore/CAMetalLayer.hpp>

#include "glfw_metal.hpp"

#import <QuartzCore/QuartzCore.h>

void glfwSetWindowMetalLayer(GLFWwindow *window, CA::MetalLayer *layer) {
    NSWindow *nsWindow = glfwGetCocoaWindow(window);
    nsWindow.contentView.layer = (__bridge CAMetalLayer*)layer;
    nsWindow.contentView.wantsLayer = YES;
}
