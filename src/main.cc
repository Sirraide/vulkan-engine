#include "../clopts/include/clopts.hh"
#include "../lib/context.hh"
#include "../lib/model.hh"
#include "../lib/renderer.hh"
#include "../lib/vertex.hh"

#include <chrono>

using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"filename", "The image to load.", std::string, false>
>; // clang-format on

int main(int argc, char** argv) {
    auto opts = options::parse(argc, argv);
    vk::context ctx{ 1280, 720, "Vulkan Template" };
    ctx.toggle_vsync(true);

    ctx.on_key_pressed = [](vk::context* ctx, int key, int scancode, int action, int mods) {
        (void) scancode;
        (void) mods;

        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(ctx->window, GLFW_TRUE);
        if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) ctx->paused = !ctx->paused;
    };

    vk::texture_renderer renderer(&ctx, "out/tex_shader_vert.spv", "out/tex_shader_frag.spv");
    vk::texture_model room_model(&renderer, "assets/viking_room.png", "assets/viking_room.obj");
    vk::texture_instance room1{ &room_model, { glm::scale(glm::translate(glm::mat4{ 1.0f }, glm::vec3(-.5f, 0.f, 0.f)), glm::vec3(.5f)) } };
    vk::texture_instance room2{ &room_model, { glm::scale(glm::translate(glm::mat4{ 1.0f }, glm::vec3(.5f, 0.f, 0.f)), glm::vec3(.5f)) } };

    vk::geometric_renderer geom_renderer(&ctx, "out/geom_shader_vert.spv", "out/geom_shader_frag.spv");
    vk::geometry rects[5] = {
        geom_renderer.build_geometry().rect({ -.9f, -.9f }, { -.8f, -.8f }),
        geom_renderer.build_geometry().rect({ -.9f, -.7f }, { -.8f, -.6f }),
        geom_renderer.build_geometry().rect({ -.9f, -.5f }, { -.8f, -.4f }),
        geom_renderer.build_geometry().rect({ -.9f, -.3f }, { -.8f, -.2f }),
        geom_renderer.build_geometry().rect({ -.9f, -.1f }, { -.8f, -0.f }),
    };

    ctx.run_forever([&](VkCommandBuffer command_buffer) {
        /// Update uniforms.
        static auto start_time = std::chrono::high_resolution_clock::now();
        static f32 time = 0;

        if (!ctx.paused) {
            auto current_time = std::chrono::high_resolution_clock::now();
            time = std::chrono::duration<f32, std::chrono::seconds::period>(current_time - start_time).count();
        }

        uniform_buffer_object* ubo;
        vkMapMemory(ctx.device, renderer.uniform_buffers_memory[ctx.current_frame], 0, sizeof *ubo, 0, (void**)&ubo);
        ubo->model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo->view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo->proj = glm::perspective(glm::radians(45.0f), f32(ctx.swap_chain_extent.width) / f32(ctx.swap_chain_extent.height), 0.1f, 10.0f);
        ubo->proj[1][1] *= -1;
        vkUnmapMemory(ctx.device, renderer.uniform_buffers_memory[ctx.current_frame]);

        renderer.draw(command_buffer, room1);
        renderer.draw(command_buffer, room2);

        static u64 rect_idx = 0;
        if (!ctx.paused) rect_idx = u64(time * 2) % (sizeof rects / sizeof *rects);
        geom_renderer.draw(command_buffer, rects[rect_idx]);
    });
}