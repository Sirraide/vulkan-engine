// clang-format off
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <map>
// clang-format on

#include "context.hh"

using namespace vk;

namespace {
/// How many contexts are currently alive.
u64 context_count = 0;

#ifdef ENABLE_VALIDATION_LAYERS
const std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation",
};
#endif

struct queue_family_indices {
    std::optional<u32> graphics_family;

    bool is_complete() const {
        return graphics_family.has_value();
    }
};

/// Find available queue families.
queue_family_indices find_queue_families(VkPhysicalDevice device) {
    queue_family_indices indices{};

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    for (u32 i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphics_family = i;
        if (indices.is_complete()) break;
    }

    return indices;
}

/// Compute the score of a given device.
u64 phys_dev_score(VkPhysicalDevice dev) {
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(dev, &props);
    vkGetPhysicalDeviceFeatures(dev, &features);

    /// Check if this device is suitable at all.
    auto indices = find_queue_families(dev);
    if (!indices.is_complete()) return 0;

    /// Compute a score for this device.
    u64 score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
    score += props.limits.maxImageDimension2D;
    return score;
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

vk::context::context() {
    if (context_count == 0) vulkan_init();
    context_count++;

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
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
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
    assert_ok(vkCreateInstance(&create_info, nullptr, &instance), "failed to create instance");

    /// Create the debug messenger.
#ifdef ENABLE_VALIDATION_LAYERS
    {
        static auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (!func) die("[Vulkan] Failed to load vkCreateDebugUtilsMessengerEXT");
        assert_ok(func(instance, &debug_create_info, nullptr, &debug_messenger), "failed to create debug messenger");
    }
#endif

    /// Determine the number of physical devices.
    u32 device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count == 0) die("[Vulkan] No devices available");

    /// Get the physical devices.
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    /// Pick the best device.
    std::multimap<u64, VkPhysicalDevice> devices_by_score;
    for (auto& device : devices) devices_by_score.insert({ phys_dev_score(device), device });
    if (devices_by_score.rbegin()->first == 0) die("[Vulkan] No suitable devices available");
    physical_device = devices_by_score.rbegin()->second;

    /// Specify the queue families for the logical device.
    auto indices = find_queue_families(physical_device);
    f32 graphics_queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = indices.graphics_family.value();
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &graphics_queue_priority;

    /// Specify the features we want to use.
    VkPhysicalDeviceFeatures device_features{};

    /// Create the logical device.
    VkDeviceCreateInfo create_info_device{};
    create_info_device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info_device.pQueueCreateInfos = &queue_create_info;
    create_info_device.queueCreateInfoCount = 1;
    create_info_device.pEnabledFeatures = &device_features;
#ifdef ENABLE_VALIDATION_LAYERS
    create_info_device.enabledLayerCount = u32(validation_layers.size());
    create_info_device.ppEnabledLayerNames = validation_layers.data();
#endif
    assert_ok(vkCreateDevice(physical_device, &create_info_device, nullptr, &dev), "failed to create logical device");

    /// Get the queue.
    vkGetDeviceQueue(dev, indices.graphics_family.value(), 0, &graphics_queue);
}

vk::context::~context() {
#ifdef ENABLE_VALIDATION_LAYERS
    {
        static auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) func(instance, debug_messenger, nullptr);
    }
#endif
    vkDestroyDevice(dev, nullptr);
    vkDestroyInstance(instance, nullptr);

    context_count--;
    if (context_count == 0) vulkan_fini();
}

void vk::context::poll() {
    glfwPollEvents();
}

void vk::context::run_forever(std::function<void()> tick) {
    while (!should_terminate) tick();
}