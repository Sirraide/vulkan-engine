#ifndef VULKAN_TEMPLATE_CONTEXT_HH
#define VULKAN_TEMPLATE_CONTEXT_HH
#define GLFW_INCLUDE_VULKAN

#include "utils.hh"

#include <functional>
#include <GLFW/glfw3.h>
#include <optional>
#include <vulkan/vulkan.h>

namespace vk {
struct queue_family_indices {
    std::optional<u32> graphics_family;
    std::optional<u32> present_family;

    bool is_complete() const {
        return graphics_family.has_value() && present_family.has_value();
    }
};

struct swap_chain_support_details {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

/// The Vulkan context.
struct context {
    /// Device handles etc.
    VkCommandBuffer nonnull command_buffer;
    VkCommandPool nonnull command_pool;
    VkDevice nonnull device;
    VkInstance nonnull instance;
    VkPhysicalDevice nonnull physical_device;
    VkPipeline nonnull graphics_pipeline;
    VkPipelineLayout nonnull pipeline_layout;
    VkRenderPass nonnull render_pass;
    VkSurfaceKHR nonnull surface;

    /// Queues.
    VkQueue nonnull graphics_queue;
    VkQueue nonnull present_queue;

    /// Swap chain.
    VkSwapchainKHR nonnull swap_chain;
    VkFormat swap_chain_image_format;
    VkExtent2D swap_chain_extent;
    std::vector<VkImage nonnull> swap_chain_images;
    std::vector<VkImageView nonnull> swap_chain_image_views;
    std::vector<VkFramebuffer nonnull> swap_chain_framebuffers;

    /// Window.
    GLFWwindow* nonnull window;
    int wd;
    int ht;

#ifdef ENABLE_VALIDATION_LAYERS
    VkDebugUtilsMessengerEXT nonnull debug_messenger;
#endif

    /// Create a new context and initialise Vulkan.
    context(int wd, int ht, std::string_view title);

    /// Destroy the context and cleanup Vulkan if there are no contexts left.
    ~context();

    /// We don't want to copy or move contexts.
    nocopy(context);
    nomove(context);


    /// Poll window events.
    void poll();

    /// Run the context forever.
    void run_forever(std::function<void()> tick = {});

    /// Whether the main loop should terminate.
    bool should_terminate();

    /// INTERNAL:
    void create_swap_chain();
    void create_render_pass();
    void create_graphics_pipeline();
    void create_framebuffers();
    void create_command_pool();
    void create_command_buffer();
    void record_command_buffer(VkCommandBuffer nonnull command_buffer, u32 img_index);

    auto create_shader_module(const std::vector<char>& code) -> VkShaderModule nonnull;
    auto find_queue_families(VkPhysicalDevice nonnull device) -> queue_family_indices;
    u64 phys_dev_score(VkPhysicalDevice nonnull dev);
    auto query_swap_chain_support(VkPhysicalDevice nonnull device) -> swap_chain_support_details;
};

} // namespace vk

#endif // VULKAN_TEMPLATE_CONTEXT_HH
