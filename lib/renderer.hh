#ifndef VULKAN_TEMPLATE_RENDERER_HH
#define VULKAN_TEMPLATE_RENDERER_HH
#include "utils.hh"
#include "model.hh"

#include <vector>

namespace vk {

struct context;

/// A renderer is responsible for drawing models etc. using
/// a specific vertex and fragment shader.
struct renderer {
    context* ctx;

    /// Descriptors.
    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;

    /// Pipeline.
    VkPipeline graphics_pipeline;
    VkPipelineLayout pipeline_layout;

    /// Textures.
    VkSampler texture_sampler;

    /// Uniforms.
    std::vector<VkBuffer> uniform_buffers;
    std::vector<VkDeviceMemory> uniform_buffers_memory;

    renderer(context* ctx, std::string_view vert_path, std::string_view frag_path,
        const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings);

    nocopy(renderer);
    renderer(renderer&& other) noexcept;
    renderer& operator=(renderer&& other) noexcept;
    ~renderer();

    /// Check if this renderer is currently bound to the context.
    bool bound() const;

    /// Create the descriptor sets for a model so it can be drawn by this renderer.
    void create_descriptor_sets(std::vector<VkDescriptorSet>& descriptor_sets, VkImageView view);

    /// Mark a renderer as already deleted.
    void deleted();

    /// Draw a model.
    void draw(VkCommandBuffer command_buffer, const model& m);

    /// INTERNAL:
    void create_texture_sampler();
    void create_descriptor_set_layout(const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings);
    void create_uniform_buffers();
    void create_descriptor_pool(const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings);
    void create_graphics_pipeline(std::string_view vert_path, std::string_view frag_path);
    auto create_shader_module(const std::vector<char>& code) -> VkShaderModule;
};
}

#endif // VULKAN_TEMPLATE_RENDERER_HH
