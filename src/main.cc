#include "../lib/context.hh"

int main() {
    vk::context ctx{800, 600, "Vulkan Template"};

    ctx.run_forever([&] {
        if (ctx.should_terminate()) return;
        ctx.poll();
    });
}