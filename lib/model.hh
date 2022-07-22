#ifndef VULKAN_TEMPLATE_MODEL_HH
#define VULKAN_TEMPLATE_MODEL_HH
#include "utils.hh"
#include "vertex_buffer.hh"

namespace vk {
struct context;
struct renderer;

struct model {
    renderer* r;

    /// Texture.
    VkImage texture_image;
    VkDeviceMemory texture_image_memory;
    VkImageView texture_image_view;
    u32 mip_levels;

    /// Descriptors.
    std::vector<VkDescriptorSet> descriptor_sets;

    /// Vertices and indices.
    vertex_buffer verts;

    model(renderer* r, std::string_view texture_path, std::string_view obj_path);
    ~model();

    /// Don't want to deal w/ this rn.
    nocopy(model);
    nomove(model);

    /// INTERNAL:
    void load_model(std::string_view obj_path);
    void load_texture(std::string_view texture_path);
};

} // namespace vk

#endif // VULKAN_TEMPLATE_MODEL_HH
