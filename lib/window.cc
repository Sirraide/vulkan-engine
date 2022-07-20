#include "window.hh"

vk::window::window(context* nonnull ctx, int wd, int ht) : ctx(ctx), handle(glfwCreateWindow(wd, ht, "Vulkan", nullptr, nullptr)), wd(wd), ht(ht) {
    if (handle == nullptr) {
        const char* err = nullptr;
        glfwGetError(&err);
        die("[GLFW Error] Could not create window: {}", err);
    }
}

vk::window::~window() {
    glfwDestroyWindow(handle);
}

bool vk::window::should_close() {
    return glfwWindowShouldClose(handle);
}