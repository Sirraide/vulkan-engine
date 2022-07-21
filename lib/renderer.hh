#ifndef VULKAN_TEMPLATE_RENDERER_HH
#define VULKAN_TEMPLATE_RENDERER_HH
#include "utils.hh"

namespace vk {

struct context;

/// A renderer is responsible for drawing models etc. using
/// a specific vertex and fragment shader.
struct renderer {
    context* nonnull ctx;

    renderer(context* nonnull ctx) : ctx(ctx) {}
};
}

#endif // VULKAN_TEMPLATE_RENDERER_HH
