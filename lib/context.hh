#ifndef VULKAN_TEMPLATE_CONTEXT_HH
#define VULKAN_TEMPLATE_CONTEXT_HH
#define GLFW_INCLUDE_VULKAN

#include "model.hh"
#include "utils.hh"
#include "vertex.hh"

#include <functional>
#include <GLFW/glfw3.h>
#include <optional>
#include <vulkan/vulkan.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "../imgui/backends/imgui_impl_glfw.h"
#include "../imgui/backends/imgui_impl_vulkan.h"
#include "../imgui/imgui.h"
#pragma GCC diagnostic pop

#define MAX_FRAMES_IN_FLIGHT 2

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

enum font_create_info_type {
    FONT_CREATE_INFO_TTF_TYPE
};

struct font_create_info {
    font_create_info_type type;
    std::string_view path;
    f32 size;
    ImFont** out_font;
    std::vector<ImWchar> ranges = { 0x20, 0xfffd, 0 };
};

/// The Vulkan context.
struct context {
    using render_callback = std::function<void(VkCommandBuffer)>;
    using kb_callback = std::function<void(context*, int, int, int, int)>;
    using cleanup_callback = std::function<void(context*)>;

    /// Device handles etc.
    VkCommandPool command_pool;
    VkDevice device;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkRenderPass render_pass;
    VkSurfaceKHR surface;

    /// Queues.
    VkQueue graphics_queue;
    VkQueue present_queue;

    /// Swap chain.
    VkSwapchainKHR swap_chain;
    VkFormat swap_chain_image_format;
    VkExtent2D swap_chain_extent;
    std::vector<VkImage> swap_chain_images;
    std::vector<VkImageView> swap_chain_image_views;
    std::vector<VkFramebuffer> swap_chain_framebuffers;

    /// Frames.
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    u32 current_frame = 0;

    /// The pipeline that is currently bound.
    VkPipeline bound_pipeline;

    /// Depth buffer.
    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;

    /// MSAA
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
    VkImage colour_image;
    VkDeviceMemory colour_image_memory;
    VkImageView colour_image_view;

    /// IMGUI.
    VkDescriptorPool imgui_descriptor_pool = VK_NULL_HANDLE;
    ImFont* main_font = nullptr;

    /// Window.
    GLFWwindow* window;
    kb_callback on_key_pressed = [](context*, int, int, int, int) {};
    bool resized = false;
    bool paused = false;

    /// Cleanup.
    /// Callbacks to call when the context is destroyed.
    /// These are called in the reverse order that they were added.
    std::vector<cleanup_callback> cleanup_callbacks;

private:
    bool vsync = true;

public:
#ifdef ENABLE_VALIDATION_LAYERS
    VkDebugUtilsMessengerEXT debug_messenger;
#endif

    /// Create a new context and initialise Vulkan.
    context(int wd, int ht, std::string_view title);

    /// Destroy the context and cleanup Vulkan if there are no contexts left.
    ~context();

    /// We don't want to copy or move contexts.
    nocopy(context);
    nomove(context);

    /// Begin/AtEnd a new IMGUI frame.
    void ImGui_Begin();
    void ImGui_End(VkCommandBuffer command_buffer);

    /// Bind a renderer to this context.
    void bind(texture_renderer& r);

    /// Add fonts to the context. This may only be called once!
    void init_fonts(const std::vector<font_create_info>& fonts);

    /// Poll window events.
    void poll();

    /// Run the context forever.
    void run_forever(render_callback tick);

    /// Whether the main loop should terminate.
    bool should_terminate();

    /// Terminate the main loop.
    void terminate();

    /// Toggle vsync.
    void toggle_vsync(bool enable_vsync);

    /// INTERNAL (Setup):
    void pick_physical_device();
    void create_logical_device();
    void create_swap_chain();
    void create_image_views();
    void create_render_pass();
    void create_framebuffers();
    void create_command_pool();
    void create_colour_resources();
    void create_depth_resources();
    void create_command_buffers();
    void create_sync_objects();
    void init_imgui();

    /// INTERNAL:
    auto begin_single_time_commands() -> VkCommandBuffer;
    void cleanup_swap_chain();
    void copy_buffer(VkBuffer dest, VkBuffer src, VkDeviceSize size);
    void copy_buffer_to_image(VkImage image, VkBuffer buffer, u32 width, u32 height);
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
        VkBuffer& buffer, VkDeviceMemory& buffer_memory);
    void create_image(u32 width, u32 height, u32 mip_lvls, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling,
        VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
        VkDeviceMemory& image_memory);
    auto create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, u32 mip_lvls) -> VkImageView;
    void draw_frame(const render_callback& tick);
    void end_single_time_commands(VkCommandBuffer command_buffer);
    auto find_depth_format() -> VkFormat;
    auto find_memory_type(u32 type_filter, VkMemoryPropertyFlags properties) -> u32;
    auto find_queue_families(VkPhysicalDevice device) -> queue_family_indices;
    auto find_supported_format(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
        VkFormatFeatureFlags features) -> VkFormat;
    void generate_mipmaps(VkImage image, VkFormat image_format, u32 wd, u32 ht, u32 mip_levels);
    auto phys_dev_score(VkPhysicalDevice dev) -> u64;
    auto query_swap_chain_support(VkPhysicalDevice device) -> swap_chain_support_details;
    void begin_recording_command_buffer(VkCommandBuffer command_buffer, u32 img_index);
    void end_recording_command_buffer(VkCommandBuffer command_buffer);
    void recreate_swap_chain();
    void transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout,
        VkImageLayout new_layout, u32 mip_lvls);
};

} // namespace vk

#endif // VULKAN_TEMPLATE_CONTEXT_HH
