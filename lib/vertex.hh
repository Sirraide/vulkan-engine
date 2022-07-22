#ifndef VULKAN_TEMPLATE_VERTEX_HH
#define VULKAN_TEMPLATE_VERTEX_HH
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan_core.h>

struct vertex {
    glm::vec3 pos;
    glm::vec3 colour;
    glm::vec3 normal;
    glm::vec2 tex_coord;

    bool operator==(const vertex& other) const = default;

    static auto binding_description() -> VkVertexInputBindingDescription {
        VkVertexInputBindingDescription binding_description = {};
        binding_description.binding = 0;
        binding_description.stride = sizeof(vertex);
        binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding_description;
    }

    static auto attribute_descriptions() -> std::array<VkVertexInputAttributeDescription, 4> {
        std::array<VkVertexInputAttributeDescription, 4> attribute_descriptions{};

        attribute_descriptions[0].binding = 0;
        attribute_descriptions[0].location = 0;
        attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[0].offset = offsetof(vertex, pos);

        attribute_descriptions[1].binding = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[1].offset = offsetof(vertex, colour);

        attribute_descriptions[2].binding = 0;
        attribute_descriptions[2].location = 2;
        attribute_descriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[2].offset = offsetof(vertex, normal);

        attribute_descriptions[3].binding = 0;
        attribute_descriptions[3].location = 3;
        attribute_descriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[3].offset = offsetof(vertex, tex_coord);

        return attribute_descriptions;
    }
};

struct uniform_buffer_object {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

template <>
struct std::hash<vertex> {
    size_t operator()(const vertex& v) const {
        return ((std::hash<glm::vec3>()(v.pos) ^ (std::hash<glm::vec3>()(v.colour) << 1)) >> 1) ^ (std::hash<glm::vec2>()(v.tex_coord) << 1);
    }
};

#endif // VULKAN_TEMPLATE_VERTEX_HH
