#ifndef VULKAN_TEMPLATE_CONTEXT_HH
#define VULKAN_TEMPLATE_CONTEXT_HH
#define GLFW_INCLUDE_VULKAN

#include "utils.hh"
#include "vertex.hh"

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

    /// Frames.
    std::vector<VkCommandBuffer nonnull> command_buffers;
    std::vector<VkSemaphore nonnull> image_available_semaphores;
    std::vector<VkSemaphore nonnull> render_finished_semaphores;
    std::vector<VkFence nonnull> in_flight_fences;
    std::vector<VkBuffer nonnull> uniform_buffers;
    std::vector<VkDeviceMemory nonnull> uniform_buffers_memory;
    u32 current_frame = 0;

    /// Vertices and indices.
    std::vector<vertex> vertices;
    VkBuffer nonnull vertex_buffer;
    VkDeviceMemory nonnull vertex_buffer_memory;

    std::vector<u32> indices;
    VkBuffer nonnull index_buffer;
    VkDeviceMemory nonnull index_buffer_memory;

    /// Uniforms.
    VkDescriptorPool nonnull descriptor_pool;
    VkDescriptorSetLayout nonnull descriptor_set_layout;
    std::vector<VkDescriptorSet nonnull> descriptor_sets;

    /// Textures.
    VkImage nonnull texture_image;
    VkDeviceMemory nonnull texture_image_memory;
    VkImageView nonnull texture_image_view;
    VkSampler nonnull texture_sampler;
    u32 mip_levels;

    /// Depth buffer.
    VkImage nonnull depth_image;
    VkDeviceMemory nonnull depth_image_memory;
    VkImageView nonnull depth_image_view;

    /// MSAA
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
    VkImage nonnull colour_image;
    VkDeviceMemory nonnull colour_image_memory;
    VkImageView nonnull colour_image_view;

    /// Window.
    GLFWwindow* nonnull window;
    bool resized = false;

    /// Miscellaneous.
    std::string filename;

#ifdef ENABLE_VALIDATION_LAYERS
    VkDebugUtilsMessengerEXT nonnull debug_messenger;
#endif

    /// Create a new context and initialise Vulkan.
    context(int wd, int ht, std::string_view title, std::string_view filename);

    /// Destroy the context and cleanup Vulkan if there are no contexts left.
    ~context();

    /// We don't want to copy or move contexts.
    nocopy(context);
    nomove(context);

    /// Poll window events.
    void poll();

    /// Run the context forever.
    void run_forever();

    /// Whether the main loop should terminate.
    bool should_terminate();

    /// INTERNAL (Setup):
    void pick_physical_device();
    void create_logical_device();
    void create_swap_chain();
    void create_image_views();
    void create_render_pass();
    void create_descriptor_set_layout();
    void create_graphics_pipeline();
    void create_framebuffers();
    void create_command_pool();
    void create_colour_resources();
    void create_depth_resources();
    void create_texture_image();
    void create_texture_image_view();
    void create_texture_sampler();
    void load_model();
    void create_vertex_buffer();
    void create_index_buffer();
    void create_uniform_buffers();
    void create_descriptor_pool();
    void create_descriptor_sets();
    void create_command_buffers();
    void create_sync_objects();

    /// INTERNAL:
    auto begin_single_time_commands() -> VkCommandBuffer nonnull;
    void cleanup_swap_chain();
    void copy_buffer(VkBuffer nonnull dest, VkBuffer nonnull src, VkDeviceSize size);
    void copy_buffer_to_image(VkImage nonnull image, VkBuffer nonnull buffer, u32 width, u32 height);
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
        VkBuffer nonnull& buffer, VkDeviceMemory nonnull& buffer_memory);
    void create_image(u32 width, u32 height, u32 mip_lvls, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling,
        VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage nonnull& image,
        VkDeviceMemory nonnull& image_memory);
    auto create_image_view(VkImage nonnull image, VkFormat format, VkImageAspectFlags aspect_flags, u32 mip_lvls) -> VkImageView nonnull;
    auto create_shader_module(const std::vector<char>& code) -> VkShaderModule nonnull;
    void draw_frame();
    void end_single_time_commands(VkCommandBuffer nonnull command_buffer);
    auto find_depth_format() -> VkFormat;
    auto find_memory_type(u32 type_filter, VkMemoryPropertyFlags properties) -> u32;
    auto find_queue_families(VkPhysicalDevice nonnull device) -> queue_family_indices;
    auto find_supported_format(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
        VkFormatFeatureFlags features) -> VkFormat;
    void generate_mipmaps(VkImage nonnull image, VkFormat image_format, u32 wd, u32 ht, u32 mip_levels);
    auto phys_dev_score(VkPhysicalDevice nonnull dev) -> u64;
    auto query_swap_chain_support(VkPhysicalDevice nonnull device) -> swap_chain_support_details;
    void record_command_buffer(VkCommandBuffer nonnull command_buffer, u32 img_index);
    void recreate_swap_chain();
    void transition_image_layout(VkImage nonnull image, VkFormat format, VkImageLayout old_layout,
        VkImageLayout new_layout, u32 mip_lvls);
    void update_uniform_buffer(u32 current_image);
};

} // namespace vk

#endif // VULKAN_TEMPLATE_CONTEXT_HH
