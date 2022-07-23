#ifndef VULKAN_TEMPLATE_MODEL_HH
#define VULKAN_TEMPLATE_MODEL_HH
#include "utils.hh"
#include "vertex_buffer.hh"

namespace vk {
struct context;
struct texture_renderer;

struct texture_model {
    texture_renderer* r;

    /// Texture.
    VkImage texture_image;
    VkDeviceMemory texture_image_memory;
    VkImageView texture_image_view;
    u32 mip_levels;

    /// Descriptors.
    std::vector<VkDescriptorSet> descriptor_sets;

    /// Vertices and indices.
    vertex_buffer verts;

    texture_model(texture_renderer* r, std::string_view texture_path, std::string_view obj_path);
    ~texture_model();

    /// Don't want to deal w/ this rn.
    nocopy(texture_model);
    nomove(texture_model);

    /// INTERNAL:
    void load_model(std::string_view obj_path);
    void load_texture(std::string_view texture_path);
};

struct texture_instance {
    /// The model that this instance uses.
    texture_model* m;

    /// The transform to apply to the model.
    push_constant constant;

    texture_instance(texture_model* m, push_constant value = {});
};

} // namespace vk

#endif // VULKAN_TEMPLATE_MODEL_HH
