#ifndef VULKAN_TEMPLATE_WINDOW_HH
#define VULKAN_TEMPLATE_WINDOW_HH
#define GLFW_INCLUDE_VULKAN
#include "context.hh"
#include "utils.hh"

#include <GLFW/glfw3.h>

namespace vk {
struct window {
    context* nonnull ctx;
    GLFWwindow* nonnull handle;
    int wd;
    int ht;

    window(context* nonnull, int wd, int ht);
    ~window();

    /// Calls glfwWindowShouldClose().
    bool should_close();

    nocopy(window);
    nomove(window);
};
} // namespace vk

#endif // VULKAN_TEMPLATE_WINDOW_HH
