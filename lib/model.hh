#ifndef VULKAN_TEMPLATE_MODEL_HH
#define VULKAN_TEMPLATE_MODEL_HH
#include "utils.hh"
#include "vertex_buffer.hh"

namespace vk {
struct context;

struct model {
    context* ctx;

    /// Texture.
    VkImage texture_image;
    VkDeviceMemory texture_image_memory;
    VkImageView texture_image_view;
    u32 mip_levels;

    /// Vertices and indices.
    vertex_buffer verts;

    model(context* ctx, std::string_view texture_path, std::string_view obj_path);
    ~model();

    /// Draw the model. Returns the number of indices.
    void draw(VkCommandBuffer command_buffer);

    /// Don't want to deal w/ this rn.
    nocopy(model);
    nomove(model);

    /// INTERNAL:
    void load_model(std::string_view obj_path);
    void load_texture(std::string_view texture_path);
};

} // namespace vk

#endif // VULKAN_TEMPLATE_MODEL_HH
