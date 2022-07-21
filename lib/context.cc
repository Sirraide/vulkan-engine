#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define STB_IMAGE_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#include "context.hh"

#include "../3rdparty/tiny_obj_loader.h"
#include "vertex.hh"

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <set>
#include <stb/stb_image.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define MODEL_PATH           "assets/viking_room.obj"
#define TEXTURE_PATH         "assets/viking_room.png"

using namespace vk;
defer_type_operator_lhs defer_type_operator_lhs::instance;

/// ======================================================================
///  Utilities
/// ======================================================================
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

bool has_stencil_component(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
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

VkSampleCountFlagBits phys_max_usable_sample_count(VkPhysicalDevice dev) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);

    auto counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    if (counts & VK_SAMPLE_COUNT_1_BIT) return VK_SAMPLE_COUNT_1_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
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
}

/// Cleanup Vulkan.
void vulkan_fini() {
    glfwTerminate();
}

} // namespace

/// ======================================================================
///  Context
/// ======================================================================
vk::context::~context() {
    cleanup_swap_chain();

    vkDestroyPipeline(device, graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(device, uniform_buffers[i], nullptr);
        vkFreeMemory(device, uniform_buffers_memory[i], nullptr);
    }

    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

    vkDestroySampler(device, texture_sampler, nullptr);

    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, render_finished_semaphores[i], nullptr);
        vkDestroySemaphore(device, image_available_semaphores[i], nullptr);
        vkDestroyFence(device, in_flight_fences[i], nullptr);
    }

    vkDestroyCommandPool(device, command_pool, nullptr);

    vkDestroyDevice(device, nullptr);

#ifdef ENABLE_VALIDATION_LAYERS
    {
        static auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) func(instance, debug_messenger, nullptr);
    }
#endif

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);

    context_count--;
    if (context_count == 0) vulkan_fini();
}

vk::context::context(int wd, int ht, std::string_view title, std::string_view filename) : filename(filename) {
    if (context_count == 0) vulkan_init();
    context_count++;

    /// Create the window
    window = glfwCreateWindow(wd, ht, title.data(), nullptr, nullptr);
    if (window == nullptr) {
        const char* err = nullptr;
        glfwGetError(&err);
        die("[GLFW] Error: Could not create window: {}", err);
    }

    /// Tell GLFW to notify us when the window is resized.
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* nonnull w, int, int) {
        auto* ctx = (vk::context * nonnull) glfwGetWindowUserPointer(w);
        ctx->resized = true;
    });

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

    /// Window.
    pick_physical_device();
    create_logical_device();
    create_command_pool();
    create_command_buffers();
    create_sync_objects();

    /// Swap chain
    create_swap_chain();
    create_image_views();
    create_render_pass();
    create_colour_resources();
    create_depth_resources();
    create_framebuffers();
}

void vk::context::pick_physical_device() {
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
    msaa_samples = phys_max_usable_sample_count(physical_device);
}

void vk::context::create_logical_device() {
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
    device_features.samplerAnisotropy = VK_TRUE;
    device_features.sampleRateShading = VK_TRUE;

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

void vk::context::create_image_views() {
    swap_chain_image_views.resize(swap_chain_images.size());

    for (u64 i = 0; i < swap_chain_images.size(); i++)
        swap_chain_image_views[i] = create_image_view(swap_chain_images[i], swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void vk::context::create_render_pass() {
    /// Attachment descriptions.
    VkAttachmentDescription colour_attachment{};
    colour_attachment.format = swap_chain_image_format;
    colour_attachment.samples = msaa_samples;
    colour_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colour_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colour_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    /// Depth attachment.
    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = find_depth_format();
    depth_attachment.samples = msaa_samples;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    /// Resolve attachment for MSAA.
    VkAttachmentDescription resolve_attachment{};
    resolve_attachment.format = swap_chain_image_format;
    resolve_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    resolve_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolve_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolve_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolve_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    /// Attachment references.
    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref{};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolve_attachment_ref{};
    resolve_attachment_ref.attachment = 2;
    resolve_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    /// Subpass.
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;
    subpass.pResolveAttachments = &resolve_attachment_ref;

    /// Subpass dependencies.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    /// Render pass.
    VkAttachmentDescription attachments[]{ colour_attachment, depth_attachment, resolve_attachment };
    VkRenderPassCreateInfo create_info_render_pass{};
    create_info_render_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info_render_pass.attachmentCount = sizeof attachments / sizeof *attachments;
    create_info_render_pass.pAttachments = attachments;
    create_info_render_pass.subpassCount = 1;
    create_info_render_pass.pSubpasses = &subpass;
    create_info_render_pass.dependencyCount = 1;
    create_info_render_pass.pDependencies = &dependency;
    assert_success(vkCreateRenderPass(device, &create_info_render_pass, nullptr, &render_pass), "failed to create render pass");
}

void vk::context::create_descriptor_set_layout() {
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

    VkDescriptorSetLayoutBinding bindings[] = { ubo_layout_binding, sampler_layout_binding };
    VkDescriptorSetLayoutCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = sizeof bindings / sizeof *bindings;
    create_info.pBindings = bindings;
    assert_success(vkCreateDescriptorSetLayout(device, &create_info, nullptr, &descriptor_set_layout), "failed to create descriptor set layout");
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
    auto binding_description = vertex::binding_description();
    auto attribute_descriptions = vertex::attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = u32(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

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
    rasteriser_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasteriser_info.depthBiasEnable = VK_FALSE;

    /// Multisampling.
    VkPipelineMultisampleStateCreateInfo multisampling_info{};
    multisampling_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling_info.sampleShadingEnable = VK_TRUE;
    multisampling_info.rasterizationSamples = msaa_samples;
    multisampling_info.minSampleShading = .2f;

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

    /// Depth stencil.
    VkPipelineDepthStencilStateCreateInfo depth_stencil_info{};
    depth_stencil_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_info.depthTestEnable = VK_TRUE;
    depth_stencil_info.depthWriteEnable = VK_TRUE;
    depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS; /// Lower depth = closer.
    depth_stencil_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_info.stencilTestEnable = VK_FALSE;

    /// Pipeline layout.
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
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
    pipeline_info.pDepthStencilState = &depth_stencil_info;
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
        VkImageView attachments[] = { colour_image_view, depth_image_view, swap_chain_image_views[i] };

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = u32(sizeof attachments / sizeof *attachments);
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

void vk::context::create_colour_resources() {
    create_image(swap_chain_extent.width, swap_chain_extent.height, 1, msaa_samples, swap_chain_image_format,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colour_image, colour_image_memory);
    colour_image_view = create_image_view(colour_image, swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void vk::context::create_depth_resources() {
    auto depth_format = find_depth_format();
    create_image(swap_chain_extent.width, swap_chain_extent.height, 1, msaa_samples, depth_format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depth_image, depth_image_memory);
    depth_image_view = create_image_view(depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void vk::context::create_texture_sampler() {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device, &properties);

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    assert_success(vkCreateSampler(device, &sampler_info, nullptr, &texture_sampler), "failed to create texture sampler");
}

void vk::context::create_uniform_buffers() {
    auto buffer_size = sizeof(uniform_buffer_object);
    uniform_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniform_buffers_memory.resize(MAX_FRAMES_IN_FLIGHT);

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uniform_buffers[i], uniform_buffers_memory[i]);
    }
}

void vk::context::create_descriptor_pool() {
    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = sizeof pool_sizes / sizeof *pool_sizes;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;
    assert_success(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool), "failed to create descriptor pool");
}

void vk::context::create_descriptor_sets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptor_set_layout);
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool;
    alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts.data();

    descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
    assert_success(vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets.data()), "failed to allocate descriptor sets");

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffers[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(uniform_buffer_object);

        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = model->texture_image_view;
        image_info.sampler = texture_sampler;

        VkWriteDescriptorSet descriptor_writes[2]{};
        descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[0].dstSet = descriptor_sets[i];
        descriptor_writes[0].dstBinding = 0;
        descriptor_writes[0].dstArrayElement = 0;
        descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_writes[0].descriptorCount = 1;
        descriptor_writes[0].pBufferInfo = &buffer_info;

        descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[1].dstSet = descriptor_sets[i];
        descriptor_writes[1].dstBinding = 1;
        descriptor_writes[1].dstArrayElement = 0;
        descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_writes[1].descriptorCount = 1;
        descriptor_writes[1].pImageInfo = &image_info;

        vkUpdateDescriptorSets(device, sizeof descriptor_writes / sizeof *descriptor_writes, descriptor_writes, 0, nullptr);
    }
}

void vk::context::create_command_buffers() {
    command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = u32(command_buffers.size());
    assert_success(vkAllocateCommandBuffers(device, &alloc_info, command_buffers.data()), "failed to allocate command buffers");
}

void vk::context::create_sync_objects() {
    image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; /// We want to start with a fence in the signaled state.

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        assert_success(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphores[i]), "failed to create semaphore");
        assert_success(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphores[i]), "failed to create semaphore");
        assert_success(vkCreateFence(device, &fence_info, nullptr, &in_flight_fences[i]), "failed to create fence");
    }
}

auto vk::context::begin_single_time_commands() -> VkCommandBuffer nonnull {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer, &begin_info);
    return command_buffer;
}

void vk::context::cleanup_swap_chain() {
    vkDestroyImageView(device, colour_image_view, nullptr);
    vkDestroyImage(device, colour_image, nullptr);
    vkFreeMemory(device, colour_image_memory, nullptr);

    vkDestroyImageView(device, depth_image_view, nullptr);
    vkDestroyImage(device, depth_image, nullptr);
    vkFreeMemory(device, depth_image_memory, nullptr);

    for (auto* framebuffer : swap_chain_framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
    for (auto* image_view : swap_chain_image_views) vkDestroyImageView(device, image_view, nullptr);

    vkDestroySwapchainKHR(device, swap_chain, nullptr);
}

void vk::context::copy_buffer(VkBuffer nonnull dest, VkBuffer nonnull src, VkDeviceSize size) {
    auto command_buffer = begin_single_time_commands();

    VkBufferCopy copy_region{};
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer, src, dest, 1, &copy_region);

    end_single_time_commands(command_buffer);
}

void vk::context::copy_buffer_to_image(VkImage nonnull image, VkBuffer nonnull buffer, u32 width, u32 height) {
    auto command_buffer = begin_single_time_commands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    end_single_time_commands(command_buffer);
}

void vk::context::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
    VkBuffer nonnull& buffer, VkDeviceMemory nonnull& buffer_memory) {
    /// Create the buffer.
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    assert_success(vkCreateBuffer(device, &buffer_info, nullptr, &buffer), "failed to create vertex buffer");

    /// Allocate the buffer.
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);
    assert_success(vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory), "failed to allocate vertex buffer memory");
    vkBindBufferMemory(device, buffer, buffer_memory, 0);
}

void vk::context::create_image(u32 width, u32 height, u32 mip_lvls, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling,
    VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage nonnull& image,
    VkDeviceMemory nonnull& image_memory) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = u32(width);
    image_info.extent.height = u32(height);
    image_info.extent.depth = 1;
    image_info.mipLevels = mip_lvls;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = samples;
    vkCreateImage(device, &image_info, nullptr, &image);

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device, image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

    assert_success(vkAllocateMemory(device, &alloc_info, nullptr, &image_memory), "failed to allocate image memory");
    vkBindImageMemory(device, image, image_memory, 0);
}

auto vk::context::create_image_view(VkImage nonnull image, VkFormat format, VkImageAspectFlags aspect_flags, u32 mip_lvls) -> VkImageView nonnull {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_flags;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_lvls;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view;
    assert_success(vkCreateImageView(device, &view_info, nullptr, &image_view), "failed to create texture image view");

    return image_view;
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
    vkWaitForFences(device, 1, &in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);

    /// Acquire an image from the swap chain.
    u32 image_index;
    auto res = vkAcquireNextImageKHR(device, swap_chain, UINT64_MAX, image_available_semaphores[current_frame], VK_NULL_HANDLE, &image_index);

    /// If the swap chain is out of date, recreate it.
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swap_chain();
        return;
    } else if (res != VK_SUBOPTIMAL_KHR) assert_success(res);

    /// Reset the fence if we are submitting work.
    vkResetFences(device, 1, &in_flight_fences[current_frame]);

    /// Record the command buffer.
    vkResetCommandBuffer(command_buffers[current_frame], 0);
    record_command_buffer(command_buffers[current_frame], image_index);

    /// Submit the command buffer.
    VkSemaphore wait_semaphores[] = { image_available_semaphores[current_frame] };
    VkSemaphore signal_semaphores[] = { render_finished_semaphores[current_frame] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    /// We need to update the uniform buffer each frame before submitting.
    update_uniform_buffer(current_frame);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[current_frame];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;
    assert_success(vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fences[current_frame]), "failed to submit command buffer");

    /// Present the image to the swap chain.
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swap_chains[] = { swap_chain };
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;
    present_info.pImageIndices = &image_index;

    res = vkQueuePresentKHR(present_queue, &present_info);

    /// If the swap chain is out of date, recreate it.
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || resized) {
        resized = false;
        recreate_swap_chain();
    } else assert_success(res);

    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void vk::context::end_single_time_commands(VkCommandBuffer nonnull command_buffer) {
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);
    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

auto vk::context::find_depth_format() -> VkFormat {
    static const std::vector<VkFormat> formats = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    return find_supported_format(formats, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

/// Find the GPU memory type that supports the required properties.
u32 vk::context::find_memory_type(u32 type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    for (u32 i = 0; i < mem_properties.memoryTypeCount; i++)
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;

    die("[Vulkan] Failed to find suitable memory type");
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

auto vk::context::find_supported_format(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
    VkFormatFeatureFlags features) -> VkFormat {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) return format;
    }

    die("[Vulkan] Failed to find supported format");
}

void vk::context::generate_mipmaps(VkImage nonnull image, VkFormat image_format, u32 wd, u32 ht, u32 mip_lvls) {
    VkFormatProperties format_props;
    vkGetPhysicalDeviceFormatProperties(physical_device, image_format, &format_props);
    if (!(format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        die("[Vulkan] Image format does not support linear filtering");

    VkCommandBuffer command_buffer = begin_single_time_commands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    i32 mip_wd = i32(wd);
    i32 mip_ht = i32(ht);

    for (u32 i = 1 /** We already have `0` **/; i < mip_lvls; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mip_wd, mip_ht, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mip_wd > 1 ? mip_wd / 2 : 1, mip_ht > 1 ? mip_ht / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(command_buffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mip_wd > 1) mip_wd /= 2;
        if (mip_ht > 1) mip_ht /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mip_lvls - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    end_single_time_commands(command_buffer);
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

    /// The device has to support the required features.
    if (!features.samplerAnisotropy) return 0;

    /// Compute a score for this device.
    u64 score = 0;

    /// Discrete GPUs have a high score.
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;

    /// Devices that support a higher MSAA sample count have a higher score.
    auto cnt = phys_max_usable_sample_count(dev);
    switch(cnt) {
        case VK_SAMPLE_COUNT_2_BIT: score += 50; break;
        case VK_SAMPLE_COUNT_4_BIT: score += 100; break;
        case VK_SAMPLE_COUNT_8_BIT: score += 150; break;
        case VK_SAMPLE_COUNT_16_BIT: score += 200; break;
        case VK_SAMPLE_COUNT_32_BIT: score += 300; break;
        case VK_SAMPLE_COUNT_64_BIT: score += 350; break;

        default: break;
    }

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

    /// The order of these must be identical to the order of the attachments.
    VkClearValue clear_values[2]{};
    clear_values[0].color = { { 0.018f, 0.016f, 0.018f, 1.0f } };
    clear_values[1].depthStencil = { 1.0f, 0 };
    render_pass_begin_info.clearValueCount = sizeof clear_values / sizeof *clear_values;
    render_pass_begin_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    {
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

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
        model->draw(command_buffer);
    }
    vkCmdEndRenderPass(command_buffer);

    assert_success(vkEndCommandBuffer(command_buffer), "failed to record command buffer");
}

void vk::context::recreate_swap_chain() {
    /// If the window has been minimised, pause rendering.
    int wd, ht;
    glfwGetFramebufferSize(window, &wd, &ht);
    while (wd == 0 || ht == 0) {
        glfwGetFramebufferSize(window, &wd, &ht);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);
    cleanup_swap_chain();

    create_swap_chain();
    create_image_views();
    create_colour_resources();
    create_depth_resources();
    create_framebuffers();
}

void vk::context::transition_image_layout(VkImage nonnull image, VkFormat format, VkImageLayout old_layout,
    VkImageLayout new_layout, u32 mip_lvls) {
    auto command_buffer = begin_single_time_commands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_lvls;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else die("unsupported layout transition");

    vkCmdPipelineBarrier(command_buffer,
        source_stage, destination_stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    end_single_time_commands(command_buffer);
}

void vk::context::update_uniform_buffer(u32 current_image) {
    static auto start_time = std::chrono::high_resolution_clock::now();
    auto current_time = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::duration<f32, std::chrono::seconds::period>(current_time - start_time).count();

    uniform_buffer_object ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f), f32(swap_chain_extent.width) / f32(swap_chain_extent.height), 0.1f, 10.0f);
    ubo.proj[1][1] *= -1;

    void* data;
    vkMapMemory(device, uniform_buffers_memory[current_image], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uniform_buffers_memory[current_image]);
}

/// ======================================================================
///  API
/// ======================================================================
void vk::context::poll() {
    glfwPollEvents();
}

void vk::context::run_forever() {
    /// Pipeline
    create_texture_sampler();
    create_descriptor_set_layout();
    create_uniform_buffers();
    create_descriptor_pool();
    create_descriptor_sets();
    create_graphics_pipeline();

    while (!should_terminate()) {
        poll();
        draw_frame();
    }

    vkDeviceWaitIdle(device);
}

bool vk::context::should_terminate() {
    return glfwWindowShouldClose(window);
}