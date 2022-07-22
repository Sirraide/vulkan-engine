#ifndef VULKAN_TEMPLATE_RENDERER_HH
#define VULKAN_TEMPLATE_RENDERER_HH
#include "model.hh"
#include "utils.hh"

#include <vector>

#define PIPELINE_CTOR_ARGS   context *ctx, std::string_view vert_path, std::string_view frag_path
#define PIPELINE_CTOR_PARAMS ctx, vert_path, frag_path
#define RENDERER_CTORS(renderer)                    \
    renderer(PIPELINE_CTOR_ARGS);                   \
    nocopy(renderer);                               \
    renderer(renderer&& other) noexcept;            \
    renderer& operator=(renderer&& other) noexcept; \
    ~renderer();

namespace vk {

struct context;

/// Base renderer pipeline.
struct pipeline {
    context* ctx;

    /// Descriptors.
    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;

    /// Pipeline.
    VkPipeline graphics_pipeline;
    VkPipelineLayout pipeline_layout;

    /// Uniforms.
    std::vector<VkBuffer> uniform_buffers;
    std::vector<VkDeviceMemory> uniform_buffers_memory;

    pipeline(PIPELINE_CTOR_ARGS, const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings);
    nocopy(pipeline);
    pipeline(pipeline&& other) noexcept;
    pipeline& operator=(pipeline&& other) noexcept;
    ~pipeline();

    /// Check if this pipeline is currently bound to the context.
    bool bound() const;

    /// Allocate the descriptor sets for initialisation by the renderer.
    void allocate_descriptor_sets(std::vector<VkDescriptorSet>& descriptor_sets);

    /// INTERNAL:
    void create_descriptor_set_layout(const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings);
    void create_uniform_buffers();
    void create_descriptor_pool(const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings);
    void create_graphics_pipeline(std::string_view vert_path, std::string_view frag_path);
    auto create_shader_module(const std::vector<char>& code) -> VkShaderModule;
};

/// Renderer for models that have both vertices and textures.
struct texture_renderer : pipeline {
    /// Textures.
    VkSampler texture_sampler;

    RENDERER_CTORS(texture_renderer);

    /// Draw a model.
    void draw(VkCommandBuffer command_buffer, const texture_model& m);

    /// Create the descriptor sets for a model.
    void create_descriptor_sets(std::vector<VkDescriptorSet>& descriptor_sets, VkImageView view);

    /// INTERNAL:
    void create_texture_sampler();
};

/// Renderer for models consisting entirely of vertices with colours and no texture.
struct geometric_renderer : pipeline {
};

} // namespace vk

#endif // VULKAN_TEMPLATE_RENDERER_HH
