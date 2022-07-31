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

/// Data that can be used by a geometric renderer.
struct geometry {
    push_constant constant;

    /// Vertices.
    vertex_buffer verts;
};

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

    /// Update the uniform buffers.
    void update_uniform_buffers(const std::function<void(uniform_buffer_object&)>& update_func);

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
    void draw(VkCommandBuffer command_buffer, const model_instance& ti);

    /// Create the descriptor sets for a model.
    void create_descriptor_sets(std::vector<VkDescriptorSet>& descriptor_sets, VkImageView view);

    /// INTERNAL:
    void create_texture_sampler();
};

/// Renderer for models consisting entirely of vertices with colours and no texture.
struct geometric_renderer : pipeline {
    /// Helper struct to build a geometry.
    struct geometry_builder {
        geometric_renderer* r;

        /// Vertex data.
        std::unordered_map<vertex, u32> unique_verts;
        std::vector<vertex> verts;
        std::vector<u32> indices;

        /// Add a vertex and return its index.
        u32 add(const vertex& v);

        /// Draw a filled rect between a and b.
        auto rect(glm::vec2 a, glm::vec2 b, glm::vec3 colour = {1.f, 1.f, 1.f}) -> geometry_builder&;

        /// Get a geometry from this builder.
        operator geometry() const;

        /// These are not really meant to be used directly.
        geometry_builder(geometric_renderer* r) : r(r) {}
        nocopy(geometry_builder);
        nomove(geometry_builder);
        ~geometry_builder() {}
    };
    /// Descriptor sets.
    std::vector<VkDescriptorSet> descriptor_sets;

    RENDERER_CTORS(geometric_renderer);

    /// Return a builder for a geometry.
    auto build_geometry() -> geometry_builder;

    /// Draw a model.
    void draw(VkCommandBuffer command_buffer, const geometry& m);
};

} // namespace vk

#endif // VULKAN_TEMPLATE_RENDERER_HH
