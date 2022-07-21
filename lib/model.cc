#include "model.hh"
#include "context.hh"
#include <stb/stb_image.h>
#include <../3rdparty/tiny_obj_loader.h>

vk::model::model(context* nonnull ctx, std::string_view texture_path, std::string_view obj_path)
    : ctx(ctx) {
     load_texture(texture_path);
     load_model(obj_path);
}

vk::model::~model() {
    vkDestroyImageView(ctx->device, texture_image_view, nullptr);
    vkDestroyImage(ctx->device, texture_image, nullptr);
    vkFreeMemory(ctx->device, texture_image_memory, nullptr);
}

void vk::model::draw(VkCommandBuffer command_buffer) {
    verts.draw(command_buffer);
}

void vk::model::load_model(std::string_view obj_path) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

#ifdef ENABLE_VALIDATION_LAYERS
    fmt::print(stderr, "[Loader] Loading model \"{}\"\n", obj_path);
#endif

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, obj_path.data())) die("[Loader] failed to load model :{}\n{}", obj_path, err);

#ifdef ENABLE_VALIDATION_LAYERS
    if (!warn.empty()) fmt::print(stderr, "\033[33m[Loader] Warning: {}\n\033[m", warn);
    if (!err.empty()) fmt::print(stderr, "\033[31m[Loader] Error: {}\n\033[m", err);
#endif

    std::unordered_map<vertex, u32> unique_vertices{};
    std::vector<vertex> vertices;
    std::vector<u32> indices;

    for (const auto& shape : shapes) {
        for (auto& index : shape.mesh.indices) {
            vertex v{};

            v.pos = {
                attrib.vertices[3 * size_t(index.vertex_index) + 0],
                attrib.vertices[3 * size_t(index.vertex_index) + 1],
                attrib.vertices[3 * size_t(index.vertex_index) + 2],
            };

            v.tex_coord = {
                attrib.texcoords[2 * size_t(index.texcoord_index) + 0],

                /// In the .obj format, a vertical coordinate of `0` indicates the bottom
                /// of the image, whereas in Vulkan `0` is the top of the image. We therefore
                /// need to invert this.
                1.0f - attrib.texcoords[2 * size_t(index.texcoord_index) + 1],
            };

            v.colour = { 1.0f, 1.0f, 1.0f };

            /// New vertex.
            if (unique_vertices.count(v) == 0) {
                unique_vertices[v] = u32(vertices.size());
                vertices.push_back(v);
            }

            indices.push_back(unique_vertices[v]);
        }
    }

    verts = vertex_buffer(ctx, vertices, indices);
}

void vk::model::load_texture(std::string_view texture_path) {
    int tex_width, tex_height, tex_channels;
    auto pixels = stbi_load(texture_path.data(), &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
    auto image_size = VkDeviceSize(tex_width) * VkDeviceSize(tex_height) * 4;
    if (!pixels) die("[STB] failed to load texture image \"{}\"", texture_path);

    mip_levels = u32(std::floor(std::log2(std::max(tex_width, tex_height)))) + 1;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    ctx->create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer, staging_buffer_memory);

    void* data;
    vkMapMemory(ctx->device, staging_buffer_memory, 0, image_size, 0, &data);
    memcpy(data, pixels, image_size);
    vkUnmapMemory(ctx->device, staging_buffer_memory);

    stbi_image_free(pixels);

    ctx->create_image(u32(tex_width), u32(tex_height), mip_levels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture_image, texture_image_memory);

    ctx->transition_image_layout(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels);
    ctx->copy_buffer_to_image(texture_image, staging_buffer, u32(tex_width), u32(tex_height));
    ctx->generate_mipmaps(texture_image, VK_FORMAT_R8G8B8A8_SRGB, u32(tex_width), u32(tex_height), mip_levels);

    vkDestroyBuffer(ctx->device, staging_buffer, nullptr);
    vkFreeMemory(ctx->device, staging_buffer_memory, nullptr);

    texture_image_view = ctx->create_image_view(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);
}
