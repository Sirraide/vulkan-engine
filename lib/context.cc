#include "context.hh"

#include <algorithm>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace vk;
defer_type_operator_lhs defer_type_operator_lhs::instance;

namespace {
/// How many contexts are currently alive.
u64 context_count = 0;

#ifdef ENABLE_VALIDATION_LAYERS
const std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation",
};
#endif

const std::vector<const char*> required_device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

/// Choose the best surface format.
VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats[0];
}

/// Choose the best present mode.
VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

/// Choose the best extent.
VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) {
    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
        return capabilities.currentExtent;
    }

    int wd, ht;
    glfwGetFramebufferSize(window, &wd, &ht);
    VkExtent2D actual_extent = { u32(wd), u32(ht) };

    actual_extent.width = std::clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actual_extent.height = std::clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actual_extent;
}

inline std::vector<char> map_file(const char* filename) {
    int fd = ::open(filename, O_RDONLY);
    if (fd < 0) [[unlikely]]
        die("open(\"{}\") failed: {}", filename, ::strerror(errno));

    struct stat s {};
    if (::fstat(fd, &s)) [[unlikely]]
        die("fstat(\"{}\") failed: {}", filename, ::strerror(errno));
    auto sz = size_t(s.st_size);
    if (sz == 0) [[unlikely]]
        return {};

    auto* mem = (char*) ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (mem == MAP_FAILED) [[unlikely]]
        die("mmap(\"{}\", {}) failed: {}", filename, sz, ::strerror(errno));
    ::close(fd);

    std::vector<char> bytes(sz);
    std::memcpy(bytes.data(), mem, sz);
    if (::munmap(mem, sz)) [[unlikely]]
        die("munmap(\"{}\", {}) failed: {}", filename, sz, ::strerror(errno));
    return bytes;
}

/// Handle Vulkan errors.
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*) {
    switch (message_severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: fmt::print(stderr, "[Vulkan] Info"); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: fmt::print(stderr, "[Vulkan] Info"); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: fmt::print(stderr, "\033[33m[Vulkan] Warning"); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: fmt::print(stderr, "\033[31m[Vulkan] Error"); break;
        default: fmt::print(stderr, "[Vulkan] "); break;
    }

    switch (message_type) {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: fmt::print(stderr, " (Validation): "); break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: fmt::print(stderr, " (Performance): "); break;
        default: fmt::print(stderr, ": "); break;
    }

    fmt::print(stderr, "{}\033[m\n", callback_data->pMessage);
    if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) std::exit(1);
    return VK_FALSE;
}

/// Initialise Vulkan.
void vulkan_init() {
    /// Initialise GLFW.
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
}

/// Cleanup Vulkan.
void vulkan_fini() {
    glfwTerminate();
}

} // namespace

vk::context::context(int wd, int ht, std::string_view title) : wd(wd), ht(ht) {
    if (context_count == 0) vulkan_init();
    context_count++;

    /// Create the window
    window = glfwCreateWindow(wd, ht, title.data(), nullptr, nullptr);
    if (window == nullptr) {
        const char* err = nullptr;
        glfwGetError(&err);
        die("[GLFW] Error: Could not create window: {}", err);
    }

    /// Make sure all required layers are available.
#ifdef ENABLE_VALIDATION_LAYERS
    u32 layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (auto* layer : validation_layers) {
        bool found = false;
        for (const auto& available_layer : available_layers) {
            if (strcmp(layer, available_layer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) die("[Vulkan] Layer {} not available", layer);
    }
#endif

    /// Information about the application.
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Vulkan Template";
    app_info.applicationVersion = VK_MAKE_API_VERSION(1, 0, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_API_VERSION(1, 0, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    /// Determine the extensions we need to enable.
    u32 exts{};
    const char** exts_ptr = glfwGetRequiredInstanceExtensions(&exts);
    std::vector<const char*> extensions(exts_ptr, exts_ptr + exts);

    /// Add the validation layers if requested.
#ifdef ENABLE_VALIDATION_LAYERS
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    /// Determine the required extensions and Layers
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = u32(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    /// Add the validation layers and set up the CreateInfo for
    /// the debug messenger; we need to do this now so we can debug
    /// the vkCreateInstance call.
#ifdef ENABLE_VALIDATION_LAYERS
    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_create_info.pfnUserCallback = vulkan_debug_callback;
    debug_create_info.pUserData = nullptr;

    /// Debug vkCreateInstance and vkDestroyInstance using the debug messenger.
    create_info.pNext = &debug_create_info;

    /// The validation layers.
    create_info.enabledLayerCount = u32(validation_layers.size());
    create_info.ppEnabledLayerNames = validation_layers.data();
#endif

    /// Create the instance.
    assert_success(vkCreateInstance(&create_info, nullptr, &instance), "failed to create instance");

    /// Create the debug messenger.
#ifdef ENABLE_VALIDATION_LAYERS
    {
        static auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (!func) die("[Vulkan] Failed to load vkCreateDebugUtilsMessengerEXT");
        assert_success(func(instance, &debug_create_info, nullptr, &debug_messenger), "failed to create debug messenger");
    }
#endif

    /// Create the surface.
    assert_success(glfwCreateWindowSurface(instance, window, nullptr, &surface), "failed to create surface");

    /// Determine the number of physical devices.
    u32 device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count == 0) die("[Vulkan] No devices available");

    /// Get the physical devices.
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    /// Pick the best device.
    std::multimap<u64, VkPhysicalDevice> devices_by_score;
    for (auto& dev : devices) devices_by_score.insert({ phys_dev_score(dev), dev });
    if (devices_by_score.rbegin()->first == 0) die("[Vulkan] No suitable devices available");
    physical_device = devices_by_score.rbegin()->second;

    /// Specify the queue families for the logical device.
    auto indices = find_queue_families(physical_device);
    f32 queue_priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<u32> unique_queue_families = { indices.graphics_family.value(), indices.present_family.value() };
    for (u32 queue_family : unique_queue_families) {
        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_create_info);
    }

    /// Specify the features we want to use.
    VkPhysicalDeviceFeatures device_features{};

    /// Create the logical device.
    VkDeviceCreateInfo create_info_device{};
    create_info_device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info_device.pQueueCreateInfos = queue_create_infos.data();
    create_info_device.queueCreateInfoCount = u32(queue_create_infos.size());
    create_info_device.pEnabledFeatures = &device_features;
    create_info_device.enabledExtensionCount = u32(required_device_extensions.size());
    create_info_device.ppEnabledExtensionNames = required_device_extensions.data();
#ifdef ENABLE_VALIDATION_LAYERS
    create_info_device.enabledLayerCount = u32(validation_layers.size());
    create_info_device.ppEnabledLayerNames = validation_layers.data();
#endif
    assert_success(vkCreateDevice(physical_device, &create_info_device, nullptr, &device), "failed to create logical device");

    /// Get the queues.
    vkGetDeviceQueue(device, indices.graphics_family.value(), 0, &graphics_queue);
    vkGetDeviceQueue(device, indices.present_family.value(), 0, &present_queue);

    /// Create the swap chain.
    create_swap_chain();

    /// Create the image views.
    swap_chain_image_views.resize(swap_chain_images.size());
    for (u64 i = 0; i < swap_chain_images.size(); i++) {
        VkImageViewCreateInfo create_info_image_view{};
        create_info_image_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info_image_view.image = swap_chain_images[i];
        create_info_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info_image_view.format = swap_chain_image_format;
        create_info_image_view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info_image_view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info_image_view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info_image_view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info_image_view.subresourceRange.baseMipLevel = 0;
        create_info_image_view.subresourceRange.levelCount = 1;
        create_info_image_view.subresourceRange.baseArrayLayer = 0;
        create_info_image_view.subresourceRange.layerCount = 1;
        assert_success(vkCreateImageView(device, &create_info_image_view, nullptr, &swap_chain_image_views[i]), "failed to create image view");
    }

    create_render_pass();
    create_graphics_pipeline();
    create_framebuffers();
    create_command_pool();
    create_command_buffer();
    create_sync_objects();
}

vk::context::~context() {
    glfwDestroyWindow(window);

    vkDestroySemaphore(device, render_finished_semaphore, nullptr);
    vkDestroySemaphore(device, image_available_semaphore, nullptr);
    vkDestroyFence(device, in_flight_fence, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    for (auto* framebuffer : swap_chain_framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyPipeline(device, graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);
    for (auto* image_view : swap_chain_image_views) vkDestroyImageView(device, image_view, nullptr);
    vkDestroySwapchainKHR(device, swap_chain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
#ifdef ENABLE_VALIDATION_LAYERS
    {
        static auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) func(instance, debug_messenger, nullptr);
    }
#endif
    vkDestroyInstance(instance, nullptr);

    context_count--;
    if (context_count == 0) vulkan_fini();
}

void vk::context::create_swap_chain() {
    /// Determine the information required for the swap chain.
    auto swap_chain_support = query_swap_chain_support(physical_device);
    VkSurfaceFormatKHR surface_format = choose_swap_surface_format(swap_chain_support.formats);
    VkPresentModeKHR present_mode = choose_swap_present_mode(swap_chain_support.present_modes);
    VkExtent2D extent = choose_swap_extent(swap_chain_support.capabilities, window);

    /// Determine the number of images in the swap chain.
    /// Make sure not to exceeed the maximum number of images.
    u32 image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount)
        image_count = swap_chain_support.capabilities.maxImageCount;

    /// Prepare creating the swap chain.
    VkSwapchainCreateInfoKHR create_info_swap_chain{};
    create_info_swap_chain.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info_swap_chain.surface = surface;
    create_info_swap_chain.minImageCount = image_count;
    create_info_swap_chain.imageFormat = surface_format.format;
    create_info_swap_chain.imageColorSpace = surface_format.colorSpace;
    create_info_swap_chain.imageExtent = extent;
    create_info_swap_chain.imageArrayLayers = 1;
    create_info_swap_chain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    /// If the graphics and present queues are different, we need to specify the sharing mode.
    auto indices = find_queue_families(physical_device);
    u32 queue_families[] = { indices.graphics_family.value(), indices.present_family.value() };
    if (indices.graphics_family != indices.present_family) {
        create_info_swap_chain.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info_swap_chain.queueFamilyIndexCount = 2;
        create_info_swap_chain.pQueueFamilyIndices = queue_families;
    } else create_info_swap_chain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    /// We don't want to apply any transformation to the images.
    create_info_swap_chain.preTransform = swap_chain_support.capabilities.currentTransform;

    /// The alpha channel is for transparency.
    create_info_swap_chain.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    /// Enable clipping.
    create_info_swap_chain.presentMode = present_mode;
    create_info_swap_chain.clipped = VK_TRUE;

    /// There is no old swap chain.
    create_info_swap_chain.oldSwapchain = VK_NULL_HANDLE;

    /// Finally, create the swap chain.
    assert_success(vkCreateSwapchainKHR(device, &create_info_swap_chain, nullptr, &swap_chain), "failed to create swap chain");

    /// Get the swap chain images.
    vkGetSwapchainImagesKHR(device, swap_chain, &image_count, nullptr);
    swap_chain_images.resize(image_count);
    vkGetSwapchainImagesKHR(device, swap_chain, &image_count, swap_chain_images.data());

    /// Store these for later.
    swap_chain_extent = extent;
    swap_chain_image_format = surface_format.format;
}

void vk::context::create_render_pass() {
    /// Attachment descriptions.
    VkAttachmentDescription colour_attachment{};
    colour_attachment.format = swap_chain_image_format;
    colour_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colour_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colour_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colour_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    /// Subpasses and attachment references.
    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    /// Subpass dependencies.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    /// Render pass.
    VkRenderPassCreateInfo create_info_render_pass{};
    create_info_render_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info_render_pass.attachmentCount = 1;
    create_info_render_pass.pAttachments = &colour_attachment;
    create_info_render_pass.subpassCount = 1;
    create_info_render_pass.pSubpasses = &subpass;
    create_info_render_pass.dependencyCount = 1;
    create_info_render_pass.pDependencies = &dependency;
    assert_success(vkCreateRenderPass(device, &create_info_render_pass, nullptr, &render_pass), "failed to create render pass");
}

void vk::context::create_graphics_pipeline() {
    /// Create the shader modules.
    auto vert_shader_module = create_shader_module(map_file("out/vert.spv"));
    auto frag_shader_module = create_shader_module(map_file("out/frag.spv"));
    defer {
        vkDestroyShaderModule(device, vert_shader_module, nullptr);
        vkDestroyShaderModule(device, frag_shader_module, nullptr);
    };

    /// Assign the shaders to their corresponding stages.
    VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";

    /// Store them for later.
    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_shader_stage_info, frag_shader_stage_info };

    /// Dynamic state.
    std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state_info{};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = u32(dynamic_states.size());
    dynamic_state_info.pDynamicStates = dynamic_states.data();

    /// Vertex input.
    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    /// Input assembly.
    VkPipelineInputAssemblyStateCreateInfo input_assembly_info{};
    input_assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_info.primitiveRestartEnable = VK_FALSE;

    /// Viewport and scissor.
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = f32(swap_chain_extent.width);
    viewport.height = f32(swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swap_chain_extent;

    VkPipelineViewportStateCreateInfo viewport_state_info{};
    viewport_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_info.viewportCount = 1;
    viewport_state_info.scissorCount = 1;

    /// Rasteriser.
    VkPipelineRasterizationStateCreateInfo rasteriser_info{};
    rasteriser_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasteriser_info.depthClampEnable = VK_FALSE;
    rasteriser_info.rasterizerDiscardEnable = VK_FALSE;
    rasteriser_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasteriser_info.lineWidth = 1.0f;
    rasteriser_info.cullMode = VK_CULL_MODE_BACK_BIT;
    rasteriser_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasteriser_info.depthBiasEnable = VK_FALSE;

    /// Multisampling.
    VkPipelineMultisampleStateCreateInfo multisampling_info{};
    multisampling_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling_info.sampleShadingEnable = VK_FALSE;
    multisampling_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling_info.minSampleShading = 1.0f;

    /// Colour blending.
    VkPipelineColorBlendAttachmentState colour_blend_attachment{};
    colour_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colour_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colour_blend_info{};
    colour_blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colour_blend_info.logicOpEnable = VK_FALSE;
    colour_blend_info.logicOp = VK_LOGIC_OP_COPY;
    colour_blend_info.attachmentCount = 1;
    colour_blend_info.pAttachments = &colour_blend_attachment;

    /// Pipeline layout.
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    assert_success(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout), "failed to create pipeline layout");

    /// Finally, create the pipeline.
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = u32(sizeof shader_stages / sizeof *shader_stages);
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly_info;
    pipeline_info.pViewportState = &viewport_state_info;
    pipeline_info.pRasterizationState = &rasteriser_info;
    pipeline_info.pMultisampleState = &multisampling_info;
    pipeline_info.pDepthStencilState = nullptr;
    pipeline_info.pColorBlendState = &colour_blend_info;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;
    assert_success(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline),
        "failed to create graphics pipeline");
}

void vk::context::create_framebuffers() {
    swap_chain_framebuffers.resize(swap_chain_image_views.size());
    for (u64 i = 0; i < swap_chain_image_views.size(); ++i) {
        VkImageView attachments[] = { swap_chain_image_views[i] };

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = swap_chain_extent.width;
        framebuffer_info.height = swap_chain_extent.height;
        framebuffer_info.layers = 1;
        assert_success(vkCreateFramebuffer(device, &framebuffer_info, nullptr, &swap_chain_framebuffers[i]), "failed to create framebuffer");
    }
}

void vk::context::create_command_pool() {
    auto indices = find_queue_families(physical_device);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = indices.graphics_family.value();
    assert_success(vkCreateCommandPool(device, &pool_info, nullptr, &command_pool), "failed to create command pool");
}

void vk::context::create_command_buffer() {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    assert_success(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer), "failed to allocate command buffer");
}

void vk::context::create_sync_objects() {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; /// We want to start with a fence in the signaled state.

    assert_success(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphore), "failed to create semaphore");
    assert_success(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphore), "failed to create semaphore");
    assert_success(vkCreateFence(device, &fence_info, nullptr, &in_flight_fence), "failed to create fence");
}

auto vk::context::create_shader_module(const std::vector<char>& code) -> VkShaderModule nonnull {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const u32*>(code.data());

    VkShaderModule shader_module;
    assert_success(vkCreateShaderModule(device, &create_info, nullptr, &shader_module), "failed to create shader module");
    return shader_module;
}

void vk::context::draw_frame() {
    /// Wait for the previous frame to finish.
    vkWaitForFences(device, 1, &in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &in_flight_fence);

    /// Acquire an image from the swap chain.
    u32 image_index;
    vkAcquireNextImageKHR(device, swap_chain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);

    /// Record the command buffer.
    vkResetCommandBuffer(command_buffer, 0);
    record_command_buffer(command_buffer, image_index);

    /// Submit the command buffer.
    VkSemaphore wait_semaphores[] = { image_available_semaphore };
    VkSemaphore signal_semaphores[] = { render_finished_semaphore };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;
    assert_success(vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fence), "failed to submit command buffer");

    /// Present the image to the swap chain.
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swap_chains[] = { swap_chain };
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;
    present_info.pImageIndices = &image_index;

    vkQueuePresentKHR(present_queue, &present_info);
}

/// Find available queue families.
auto vk::context::find_queue_families(VkPhysicalDevice dev) -> queue_family_indices {
    queue_family_indices indices{};

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_family_count, queue_families.data());

    for (u32 i = 0; i < queue_family_count; i++) {
        /// Check if the queue family supports graphics.
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphics_family = i;

        /// Check if the queue family supports presentation.
        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present_support);
        if (present_support) indices.present_family = i;

        /// If we have both, we're done.
        if (indices.is_complete()) break;
    }

    return indices;
}

/// Compute the score of a given device.
/// A score of 0 indicates that the device is unsuitable.
u64 vk::context::phys_dev_score(VkPhysicalDevice dev) {
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(dev, &props);
    vkGetPhysicalDeviceFeatures(dev, &features);

    /// Check if this device supports the required queue families.
    auto indices = find_queue_families(dev);
    if (!indices.is_complete()) return 0;

    /// Get the extensions supported by this device.
    u32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extension_count, available_extensions.data());

    /// If the device doesn't have all the required extensions, it's not suitable.
    std::set<std::string> required_extensions(required_device_extensions.begin(), required_device_extensions.end());
    for (auto& ext : available_extensions) required_extensions.erase(ext.extensionName);
    if (!required_extensions.empty()) return 0;

    /// Make sure the device supports a swap chain that is compatible with the surface.
    auto swap_chain_support = query_swap_chain_support(dev);
    if (swap_chain_support.formats.empty() || swap_chain_support.present_modes.empty()) return 0;

    /// Compute a score for this device.
    u64 score = 0;

    /// Discrete GPUs have a high score.
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;

    /// Add a score for the maximum resolution.
    score += props.limits.maxImageDimension2D;

    /// If the device supports drawing and presenting, add a score.
    if (indices.graphics_family == indices.present_family) score += 100;
    return score;
}

auto vk::context::query_swap_chain_support(VkPhysicalDevice _Nonnull device) -> swap_chain_support_details {
    swap_chain_support_details details{};

    /// Get the surface capabilities.
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    /// Get the surface formats.
    u32 format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count > 0) {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    /// Get the surface presentation modes.
    u32 present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
    if (present_mode_count > 0) {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
    }

    return details;
}

void vk::context::record_command_buffer(VkCommandBuffer nonnull command_buffer, u32 img_index) {
    /// Begin recording the command buffer.
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    assert_success(vkBeginCommandBuffer(command_buffer, &begin_info));

    /// Start a render pass.
    VkRenderPassBeginInfo render_pass_begin_info{};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.framebuffer = swap_chain_framebuffers[img_index];
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = swap_chain_extent;

    VkClearValue clear_colour = { { { 0.18f, 0.16f, 0.18f, 1.0f } } };
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_colour;
    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    /// Bind the graphics pipeline.
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = float(swap_chain_extent.width);
    viewport.height = float(swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swap_chain_extent;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    /// Draw the triangle.
    vkCmdDraw(command_buffer, 3, 1, 0, 0);

    /// End the render pass.
    vkCmdEndRenderPass(command_buffer);
    assert_success(vkEndCommandBuffer(command_buffer), "failed to record command buffer");
}

///
/// API
///

void vk::context::poll() {
    glfwPollEvents();
}

void vk::context::run_forever() {
    while (!should_terminate()) {
        poll();
        draw_frame();
    }

    vkDeviceWaitIdle(device);
}

bool vk::context::should_terminate() {
    return glfwWindowShouldClose(window);
}