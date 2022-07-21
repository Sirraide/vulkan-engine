#include "../lib/context.hh"
#include "../lib/vertex.hh"
#include "../clopts/include/clopts.hh"

using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"filename", "The image to load.">
>; // clang-format on

int main(int argc, char** argv) {
    auto opts = options::parse(argc, argv);

    vk::context ctx{800, 600, "Vulkan Template", opts.get<"filename">()};
    ctx.run_forever();
}