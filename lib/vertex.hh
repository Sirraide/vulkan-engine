#ifndef VULKAN_TEMPLATE_VERTEX_HH
#define VULKAN_TEMPLATE_VERTEX_HH
#include <array>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

struct vertex {
    glm::vec2 pos;
    glm::vec3 colour;
    glm::vec2 tex_coord;

    static auto binding_description() -> VkVertexInputBindingDescription {
        VkVertexInputBindingDescription binding_description = {};
        binding_description.binding = 0;
        binding_description.stride = sizeof(vertex);
        binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding_description;
    }

    static auto attribute_descriptions() -> std::array<VkVertexInputAttributeDescription, 3> {
        std::array<VkVertexInputAttributeDescription, 3> attribute_descriptions{};

        attribute_descriptions[0].binding = 0;
        attribute_descriptions[0].location = 0;
        attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[0].offset = offsetof(vertex, pos);

        attribute_descriptions[1].binding = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[1].offset = offsetof(vertex, colour);

        attribute_descriptions[2].binding = 0;
        attribute_descriptions[2].location = 2;
        attribute_descriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[2].offset = offsetof(vertex, tex_coord);

        return attribute_descriptions;
    }
};

struct uniform_buffer_object {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

#endif // VULKAN_TEMPLATE_VERTEX_HH
