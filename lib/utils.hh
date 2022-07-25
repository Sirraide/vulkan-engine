#ifndef VULKAN_TEMPLATE_UTILS_HH
#define VULKAN_TEMPLATE_UTILS_HH
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "utils_base.hh"

#include <vulkan/vulkan_core.h>

template <typename... args_t>
inline void assert_success(VkResult res, fmt::format_string<args_t...> fmt_str = "", args_t&&... args) {
    if (res != VK_SUCCESS) {
        fmt::print(stderr, "[Vulkan] ");
        fmt::print(stderr, fmt_str, std::forward<args_t>(args)...);
        fmt::print(stderr, "\n");
        std::exit(1);
    }
}

#endif