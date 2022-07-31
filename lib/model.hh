#ifndef VULKAN_TEMPLATE_MODEL_HH
#define VULKAN_TEMPLATE_MODEL_HH
#include "utils.hh"
#include "vertex_buffer.hh"

namespace vk {
struct context;
struct texture_renderer;

struct model {
    texture_renderer* r;

    /// Texture.
    int tex_width, tex_height, tex_channels;
    VkImage texture_image;
    VkDeviceMemory texture_image_memory;
    VkImageView texture_image_view;
    u32 mip_levels;

    /// Descriptors.
    std::vector<VkDescriptorSet> descriptor_sets;

    /// Vertices and indices.
    vertex_buffer verts;

    model(texture_renderer* r, std::string_view texture_path, std::string_view obj_path);
    model(texture_renderer* r, std::string_view texture_path, glm::vec3 pos);
    ~model();

    /// Don't want to deal w/ this rn.
    nocopy(model);
    nomove(model);

    /// INTERNAL:
    void load_model(std::string_view obj_path);
    void load_texture(std::string_view texture_path);
};

struct model_instance {
    /// The model that this instance uses.
    model* m;

    /// The transform to apply to the model.
    push_constant constant;

    model_instance(model* m, push_constant value = {});
};

} // namespace vk

#endif // VULKAN_TEMPLATE_MODEL_HH
