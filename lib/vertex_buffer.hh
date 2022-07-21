#ifndef VULKAN_TEMPLATE_VERTEX_BUFFER_HH
#define VULKAN_TEMPLATE_VERTEX_BUFFER_HH
#include "utils.hh"
#include "vertex.hh"

#include <vector>

namespace vk {
struct context;

struct vertex_buffer {
    context* ctx = nullptr;

    VkBuffer vk_vertbuf;
    VkBuffer vk_idxbuf;

    VkDeviceMemory vk_vertbuf_mem;
    VkDeviceMemory vk_idxbuf_mem;

    /// For the bind call.
    VkDeviceSize offsets = {0};

    u64 index_count;

    vertex_buffer() {}
    vertex_buffer(context* ctx, const std::vector<vertex>& vertices, const std::vector<u32>& indices);
    vertex_buffer(vertex_buffer&& other);
    vertex_buffer& operator=(vertex_buffer&& other) noexcept;
    ~vertex_buffer();


    nocopy(vertex_buffer);

    /// Draw the contents of the vertex buffer.
    void bind(VkCommandBuffer command_buffer);
};

}

#endif // VULKAN_TEMPLATE_VERTEX_BUFFER_HH
