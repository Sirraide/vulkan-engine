#include "vertex_buffer.hh"

#include "context.hh"

vk::vertex_buffer::vertex_buffer(context* ctx, const std::vector<vertex>& vertices, const std::vector<u32>& indices) : ctx(ctx) {
    /// Vertex buffer.
    {
        auto buffer_size = sizeof(vertices[0]) * vertices.size();

        /// Create a staging buffer.
        VkBuffer staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        ctx->create_buffer(buffer_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging_buffer,
            staging_buffer_memory);

        /// Copy the data to the buffer.
        void* data;
        vkMapMemory(ctx->device, staging_buffer_memory, 0, buffer_size, 0, &data);
        memcpy(data, vertices.data(), (u64) buffer_size);
        vkUnmapMemory(ctx->device, staging_buffer_memory);

        /// Create the vertex buffer.
        ctx->create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vk_vertbuf, vk_vertbuf_mem);

        /// Copy the data to the buffer and delete the staging buffer.
        ctx->copy_buffer(vk_vertbuf, staging_buffer, buffer_size);
        vkDestroyBuffer(ctx->device, staging_buffer, nullptr);
        vkFreeMemory(ctx->device, staging_buffer_memory, nullptr);
    }

    /// Index buffer.
    {
        auto buffer_size = sizeof(indices[0]) * indices.size();

        /// Create a staging buffer.
        VkBuffer staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        ctx->create_buffer(buffer_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging_buffer,
            staging_buffer_memory);

        /// Copy the data to the buffer.
        void* data;
        vkMapMemory(ctx->device, staging_buffer_memory, 0, buffer_size, 0, &data);
        memcpy(data, indices.data(), (u64) buffer_size);
        vkUnmapMemory(ctx->device, staging_buffer_memory);

        /// Create the vertex buffer.
        ctx->create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vk_idxbuf, vk_idxbuf_mem);

        /// Copy the data to the buffer and delete the staging buffer.
        ctx->copy_buffer(vk_idxbuf, staging_buffer, buffer_size);
        vkDestroyBuffer(ctx->device, staging_buffer, nullptr);
        vkFreeMemory(ctx->device, staging_buffer_memory, nullptr);

        index_count = indices.size();
    }
}

vk::vertex_buffer::vertex_buffer(vertex_buffer&& other) {
    *this = std::move(other);
}

auto vk::vertex_buffer::operator=(vertex_buffer&& other)  noexcept -> vertex_buffer& {
#ifdef ENABLE_VALIDATION_LAYERS
    if (this == std::addressof(other)) die("Trying to move a vertex buffer to itself.");
#endif

    ctx = other.ctx;
    vk_vertbuf = other.vk_vertbuf;
    vk_idxbuf = other.vk_idxbuf;
    vk_vertbuf_mem = other.vk_vertbuf_mem;
    vk_idxbuf_mem = other.vk_idxbuf_mem;
    offsets = other.offsets;
    index_count = other.index_count;

    other.ctx = nullptr;
#ifdef ENABLE_VALIDATION_LAYERS
    other.vk_vertbuf = VK_NULL_HANDLE;
    other.vk_idxbuf = VK_NULL_HANDLE;
    other.vk_vertbuf_mem = VK_NULL_HANDLE;
    other.vk_idxbuf_mem = VK_NULL_HANDLE;
    other.offsets = 0;
    other.index_count = 0;
#endif

    return *this;
}

vk::vertex_buffer::~vertex_buffer() {
    if (ctx) {
        vkDestroyBuffer(ctx->device, vk_idxbuf, nullptr);
        vkFreeMemory(ctx->device, vk_idxbuf_mem, nullptr);

        vkDestroyBuffer(ctx->device, vk_vertbuf, nullptr);
        vkFreeMemory(ctx->device, vk_vertbuf_mem, nullptr);
    }
}

void vk::vertex_buffer::bind(VkCommandBuffer command_buffer) const {
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vk_vertbuf, &offsets);
    vkCmdBindIndexBuffer(command_buffer, vk_idxbuf, 0, VK_INDEX_TYPE_UINT32);
}
