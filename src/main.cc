#include "../clopts/include/clopts.hh"
#include "../lib/context.hh"
#include "../lib/model.hh"
#include "../lib/vertex.hh"

using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"filename", "The image to load.">
>; // clang-format on

int main(int argc, char** argv) {
    auto opts = options::parse(argc, argv);
    vk::context ctx{800, 600, "Vulkan Template", opts.get<"filename">()};
    ctx.run_forever();
    //vk::context ctx{800, 600, "Vulkan Template"};
    /*vk::model room_model(ctx, "assets/viking_room.obj", "assets/viking_room.png");
    vk::renderer(ctx, "out/vert.spv", "out/frag.spv");

    ctx.run_forever([&]{
        renderer.draw(room_model);
    });*/
}