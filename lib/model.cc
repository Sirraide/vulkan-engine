#include "model.hh"

#include "context.hh"
#include "renderer.hh"

#include <../3rdparty/tiny_obj_loader.h>
#include <filesystem>
#include <stb/stb_image.h>
namespace fs = std::filesystem;

vk::model_instance::model_instance(model* m, push_constant value) : m(m), constant(value) {}

vk::model::model(texture_renderer* r, std::string_view texture_path, std::string_view obj_path)
    : r(r) {
    load_texture(texture_path);
    load_model(obj_path);
    r->create_descriptor_sets(descriptor_sets, texture_image_view);
}

vk::model::model(texture_renderer* r, std::string_view texture_path, glm::vec3 pos) : r(r) {
    load_texture(texture_path);

    /// Create a vertex buffer for the texture.
    f32 wd, ht;
    if (tex_height > tex_width) {
        ht = 1.f;
        wd = f32(tex_width) / f32(tex_height);
    } else {
        wd = 1.f;
        ht = f32(tex_height) / f32(tex_width);
    }

    std::vector<vertex> vs;
    vs.push_back({ { pos.x, pos.y, 1.0f }, {}, {}, { 0.0f, 0.0f } });
    vs.push_back({ { 0.0f, pos.y + ht, 1.0f }, {}, {}, { 0.0f, 1.0f } });
    vs.push_back({ { pos.x + wd, pos.y + ht, 1.0f }, {}, {}, { 1.0f, 1.0f } });
    vs.push_back({ { pos.x + wd, pos.y, 1.0f }, {}, {}, { 1.0f, 0.0f } });
    verts = vertex_buffer(r->ctx, vs, QUAD_VERTICES);

    r->create_descriptor_sets(descriptor_sets, texture_image_view);
}

vk::model::~model() {
    vkDestroyImageView(r->ctx->device, texture_image_view, nullptr);
    vkDestroyImage(r->ctx->device, texture_image, nullptr);
    vkFreeMemory(r->ctx->device, texture_image_memory, nullptr);
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

            if (index.vertex_index >= 0) {
                v.pos = {
                    attrib.vertices[3 * size_t(index.vertex_index) + 0],
                    attrib.vertices[3 * size_t(index.vertex_index) + 1],
                    attrib.vertices[3 * size_t(index.vertex_index) + 2],
                };

                v.colour = {
                    attrib.colors[3 * size_t(index.vertex_index) + 0],
                    attrib.colors[3 * size_t(index.vertex_index) + 1],
                    attrib.colors[3 * size_t(index.vertex_index) + 2],
                };
            }

            if (index.normal_index >= 0) {
                v.normal = {
                    attrib.normals[3 * size_t(index.normal_index) + 0],
                    attrib.normals[3 * size_t(index.normal_index) + 1],
                    attrib.normals[3 * size_t(index.normal_index) + 2],
                };
            }

            if (index.texcoord_index >= 0) {
                v.tex_coord = {
                    attrib.texcoords[2 * size_t(index.texcoord_index) + 0],

                    /// In the .obj format, a vertical coordinate of `0` indicates the bottom
                    /// of the image, whereas in Vulkan `0` is the top of the image. We therefore
                    /// need to invert this.
                    1.0f - attrib.texcoords[2 * size_t(index.texcoord_index) + 1],
                };
            }

            /// New vertex.
            if (unique_vertices.count(v) == 0) {
                unique_vertices[v] = u32(vertices.size());
                vertices.push_back(v);
            }

            indices.push_back(unique_vertices[v]);
        }
    }

    verts = vertex_buffer(r->ctx, vertices, indices);
}

void vk::model::load_texture(std::string_view texture_path) {
    static const stbi_uc default_texture_pixels[4] = { 255, 255, 255, 255 };
    stbi_uc* pixels;
    VkDeviceSize image_size;

    /// Only load the texture if it exists.
    bool file_exists = fs::exists(texture_path);
    if (file_exists) {
        pixels = stbi_load(texture_path.data(), &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
        image_size = VkDeviceSize(tex_width) * VkDeviceSize(tex_height) * 4;
        if (!pixels) die("[STB] failed to load texture image \"{}\"", texture_path);
    }

    /// Otherwise, create a dummy texture.
    else {
        tex_width = 1;
        tex_height = 1;
        tex_channels = 4;
        pixels = const_cast<stbi_uc*>(default_texture_pixels);
        image_size = 4;
    }

    mip_levels = u32(std::floor(std::log2(std::max(tex_width, tex_height)))) + 1;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    r->ctx->create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer, staging_buffer_memory);

    void* data;
    vkMapMemory(r->ctx->device, staging_buffer_memory, 0, image_size, 0, &data);
    memcpy(data, pixels, image_size);
    vkUnmapMemory(r->ctx->device, staging_buffer_memory);

    if (file_exists) stbi_image_free(pixels);

    r->ctx->create_image(u32(tex_width), u32(tex_height), mip_levels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture_image, texture_image_memory);

    r->ctx->transition_image_layout(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels);
    r->ctx->copy_buffer_to_image(texture_image, staging_buffer, u32(tex_width), u32(tex_height));
    r->ctx->generate_mipmaps(texture_image, VK_FORMAT_R8G8B8A8_SRGB, u32(tex_width), u32(tex_height), mip_levels);

    vkDestroyBuffer(r->ctx->device, staging_buffer, nullptr);
    vkFreeMemory(r->ctx->device, staging_buffer_memory, nullptr);

    texture_image_view = r->ctx->create_image_view(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);
}
