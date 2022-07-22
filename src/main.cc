#include "../clopts/include/clopts.hh"
#include "../lib/context.hh"
#include "../lib/model.hh"
#include "../lib/renderer.hh"
#include "../lib/vertex.hh"
#include <chrono>

using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"filename", "The image to load.">
>; // clang-format on

int main(int argc, char** argv) {
    auto opts = options::parse(argc, argv);
    vk::context ctx{ 800, 600, "Vulkan Template" };

    VkDescriptorSetLayoutBinding ubo_layout_binding{};
    ubo_layout_binding.binding = 0;
    ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_layout_binding.descriptorCount = 1; /// Dimension.
    ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding sampler_layout_binding{};
    sampler_layout_binding.binding = 1;
    sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_layout_binding.descriptorCount = 1; /// Dimension.
    sampler_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    vk::renderer renderer(&ctx, "out/tex_vert.spv", "out/tex_frag.spv", { ubo_layout_binding, sampler_layout_binding });

    vk::model room_model(&renderer, "assets/viking_room.png", "assets/viking_room.obj");

    ctx.run_forever([&](VkCommandBuffer command_buffer) {
        /// Update uniforms.
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        auto time = std::chrono::duration<f32, std::chrono::seconds::period>(current_time - start_time).count();

        uniform_buffer_object ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), f32(ctx.swap_chain_extent.width) / f32(ctx.swap_chain_extent.height), 0.1f, 10.0f);
        ubo.proj[1][1] *= -1;

        void* data;
        vkMapMemory(ctx.device, renderer.uniform_buffers_memory[ctx.current_frame], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(ctx.device, renderer.uniform_buffers_memory[ctx.current_frame]);

        renderer.draw(command_buffer, room_model);
    });
}