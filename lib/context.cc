#include "context.hh"

#include <algorithm>
#include <map>
#include <set>

using namespace vk;

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
}

vk::context::~context() {
    glfwDestroyWindow(window);

#ifdef ENABLE_VALIDATION_LAYERS
    {
        static auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) func(instance, debug_messenger, nullptr);
    }
#endif
    vkDestroySwapchainKHR(device, swap_chain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    context_count--;
    if (context_count == 0) vulkan_fini();
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

///
/// API
///

void vk::context::poll() {
    glfwPollEvents();
}

void vk::context::run_forever(std::function<void()> tick) {
    while (!should_terminate()) tick();
}

bool vk::context::should_terminate() {
    return glfwWindowShouldClose(window);
}