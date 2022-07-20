#ifndef VULKAN_TEMPLATE_CONTEXT_HH
#define VULKAN_TEMPLATE_CONTEXT_HH
#include "utils.hh"

#include <functional>
#include <optional>
#include <vulkan/vulkan.h>

namespace vk {

/// The Vulkan context.
struct context {
    /// Whether this context should stop running.
    VkDevice dev;
    VkInstance instance;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;

    VkQueue graphics_queue;

    bool should_terminate = false;

#ifdef ENABLE_VALIDATION_LAYERS
    VkDebugUtilsMessengerEXT debug_messenger;
#endif

    /// Create a new context and initialise Vulkan.
    context();

    /// Destroy the context and cleanup Vulkan if there are no contexts left.
    ~context();

    /// We don't want to copy or move contexts.
    nocopy(context);
    nomove(context);

    /// Poll window events.
    void poll();

    /// Run the context forever.
    void run_forever(std::function<void()> tick = {});
};

} // namespace vk

#endif // VULKAN_TEMPLATE_CONTEXT_HH
