#include "../lib/context.hh"
#include "../lib/vertex.hh"

int main() {
    vk::context ctx{800, 600, "Vulkan Template"};
    ctx.run_forever();
}