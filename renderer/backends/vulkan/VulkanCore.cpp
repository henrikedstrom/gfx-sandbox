// Class Header
#include "VulkanCore.h"

// Standard Library Headers
#include <cstring>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Storage for the dynamic dispatch loader.
// Must be defined in exactly one translation unit.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

//----------------------------------------------------------------------
// Internal Utility Functions

namespace {

//----------------------------------------------------------------------
// Validation Layer Configuration

#if defined(NDEBUG)
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

const std::vector<const char*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};

bool CheckValidationLayerSupport() {
    auto availableLayers = vk::enumerateInstanceLayerProperties();
    for (const char* layerName : kValidationLayers) {
        bool found = false;
        for (const auto& layerProperties : availableLayers) {
            if (std::strcmp(layerName, layerProperties.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

//----------------------------------------------------------------------
// Required Extensions

std::vector<const char*> GetRequiredInstanceExtensions() {
    // Get extensions required by GLFW for surface creation
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    // Add debug utils extension for validation layer messages
    if (kEnableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Add portability enumeration for MoltenVK on macOS
#if defined(__APPLE__)
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    return extensions;
}

//----------------------------------------------------------------------
// Debug Messenger Callback

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, [[maybe_unused]] void* pUserData) {
    // Select output stream based on severity
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "[Vulkan Error] " << pCallbackData->pMessage << std::endl;
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[Vulkan Warning] " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

vk::DebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo() {
    return vk::DebugUtilsMessengerCreateInfoEXT{
        {}, // flags
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        DebugMessengerCallback};
}

//----------------------------------------------------------------------
// Device Extensions

std::vector<const char*> GetRequiredDeviceExtensions() {
    std::vector<const char*> extensions;

    // Swapchain extension is required for presentation
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Portability subset extension for MoltenVK on macOS
#if defined(__APPLE__)
    extensions.push_back("VK_KHR_portability_subset");
#endif

    return extensions;
}

//----------------------------------------------------------------------
// Queue Family Finding

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

QueueFamilyIndices FindQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
    QueueFamilyIndices indices;

    auto queueFamilies = device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        // Check for graphics support
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }

        // Check for present support
        if (device.getSurfaceSupportKHR(i, surface)) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}

//----------------------------------------------------------------------
// Device Suitability Checking

bool CheckDeviceExtensionSupport(vk::PhysicalDevice device) {
    auto availableExtensions = device.enumerateDeviceExtensionProperties();
    auto requiredExtensions = GetRequiredDeviceExtensions();

    std::set<std::string> requiredSet(requiredExtensions.begin(), requiredExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredSet.erase(extension.extensionName);
    }

    return requiredSet.empty();
}

bool IsDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
    QueueFamilyIndices indices = FindQueueFamilies(device, surface);
    bool extensionsSupported = CheckDeviceExtensionSupport(device);

    // Check for swapchain support
    bool swapchainAdequate = false;
    if (extensionsSupported) {
        auto formats = device.getSurfaceFormatsKHR(surface);
        auto presentModes = device.getSurfacePresentModesKHR(surface);
        swapchainAdequate = !formats.empty() && !presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapchainAdequate;
}

//----------------------------------------------------------------------
// Physical Device Selection

vk::PhysicalDevice SelectPhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface) {
    auto devices = instance.enumeratePhysicalDevices();

    if (devices.empty()) {
        throw std::runtime_error("No Vulkan-compatible physical devices found.");
    }

    // Prefer discrete GPU if available
    for (const auto& device : devices) {
        if (IsDeviceSuitable(device, surface)) {
            auto properties = device.getProperties();
            if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                VK_LOG_INFO("Selected discrete GPU: {}", properties.deviceName.data());
                return device;
            }
        }
    }

    // Fallback to any suitable device
    for (const auto& device : devices) {
        if (IsDeviceSuitable(device, surface)) {
            auto properties = device.getProperties();
            VK_LOG_INFO("Selected device: {}", properties.deviceName.data());
            return device;
        }
    }

    throw std::runtime_error("No suitable Vulkan physical device found.");
}

} // namespace

//----------------------------------------------------------------------
// Construction / Destruction

VulkanCore::VulkanCore(GLFWwindow* window) {
    // Initialize the dynamic dispatcher.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(_context.getDispatcher()->vkGetInstanceProcAddr);

    // Create the Vulkan instance.
    if (kEnableValidationLayers && !CheckValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested but not available.");
    }

    vk::ApplicationInfo appInfo{};
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto extensions = GetRequiredInstanceExtensions();

    vk::InstanceCreateFlags instanceFlags{};
#if defined(__APPLE__)
    instanceFlags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

    vk::InstanceCreateInfo createInfo{
        instanceFlags,                            // flags
        &appInfo,                                 // pApplicationInfo
        0,                                        // enabledLayerCount (set below if needed)
        nullptr,                                  // ppEnabledLayerNames (set below if needed)
        static_cast<uint32_t>(extensions.size()), // enabledExtensionCount
        extensions.data()};                       // ppEnabledExtensionNames

    // Chain debug messenger for instance creation/destruction messages
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (kEnableValidationLayers) {
        createInfo.setEnabledLayerCount(static_cast<uint32_t>(kValidationLayers.size()));
        createInfo.setPpEnabledLayerNames(kValidationLayers.data());
        debugCreateInfo = MakeDebugMessengerCreateInfo();
        createInfo.setPNext(&debugCreateInfo);
    }

    _instance = vk::raii::Instance(_context, createInfo);

    // Load instance-level functions into the dispatcher
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*_instance);

    // Create debug messenger (only in debug builds)
    if (kEnableValidationLayers) {
        _debugMessenger =
            vk::raii::DebugUtilsMessengerEXT(_instance, MakeDebugMessengerCreateInfo());
    }

    VK_LOG_INFO("Instance created successfully.");

    // Create window surface via GLFW
    VkSurfaceKHR surfaceHandle = VK_NULL_HANDLE;
    vk::Result result =
        vk::Result(glfwCreateWindowSurface(static_cast<VkInstance>(*_instance), window,
                                           nullptr, // Use default allocator
                                           &surfaceHandle));

    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create GLFW window surface.");
    }

    // Wrap the surface handle in a RAII wrapper
    _surface = vk::raii::SurfaceKHR(_instance, vk::SurfaceKHR(surfaceHandle));

    // Select physical device with required queue families
    vk::PhysicalDevice selectedDevice = SelectPhysicalDevice(*_instance, *_surface);
    _physicalDevice =
        vk::raii::PhysicalDevice(_instance, static_cast<VkPhysicalDevice>(selectedDevice));

    // Find queue family indices
    QueueFamilyIndices queueIndices = FindQueueFamilies(*_physicalDevice, *_surface);

    if (!queueIndices.isComplete()) {
        throw std::runtime_error("Failed to find required queue families.");
    }

    _graphicsQueueFamily = queueIndices.graphicsFamily.value();
    _presentQueueFamily = queueIndices.presentFamily.value();

    VK_LOG_INFO("Physical device selected. Graphics queue: {}, Present queue: {}",
                _graphicsQueueFamily, _presentQueueFamily);

    // Create logical device and retrieve queues

    // Collect unique queue families (graphics and present may be the same)
    std::set<uint32_t> uniqueQueueFamilies = {_graphicsQueueFamily, _presentQueueFamily};

    // Create QueueCreateInfos for each unique queue family
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Specify device features to enable
    vk::PhysicalDeviceFeatures deviceFeatures{};
    // TODO: Enable features as needed here (e.g., samplerAnisotropy, geometryShader)

    // Get required device extensions
    auto deviceExtensions = GetRequiredDeviceExtensions();

    // Create device create info
    vk::DeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Enable validation layers on device (for compatibility with older implementations)
    if (kEnableValidationLayers) {
        deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        deviceCreateInfo.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        deviceCreateInfo.enabledLayerCount = 0;
    }

    // Create the logical device
    _device = vk::raii::Device(_physicalDevice, deviceCreateInfo);

    // Load device-level functions into the dispatcher
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*_device);

    // Retrieve queue handles from the device
    _graphicsQueue = vk::raii::Queue(_device, _graphicsQueueFamily, 0);
    _presentQueue = vk::raii::Queue(_device, _presentQueueFamily, 0);

    VK_LOG_INFO("Logical device and queues created successfully.");
}

VulkanCore::~VulkanCore() {
    // vk::raii types handle cleanup automatically in reverse declaration order.
    VK_LOG_INFO("Destroyed.");
}

//----------------------------------------------------------------------
// Accessors

vk::Instance VulkanCore::GetInstance() const {
    return *_instance;
}

vk::PhysicalDevice VulkanCore::GetPhysicalDevice() const {
    return *_physicalDevice;
}

vk::Device VulkanCore::GetDevice() const {
    return *_device;
}

vk::Queue VulkanCore::GetGraphicsQueue() const {
    return *_graphicsQueue;
}

vk::Queue VulkanCore::GetPresentQueue() const {
    return *_presentQueue;
}

vk::SurfaceKHR VulkanCore::GetSurface() const {
    return *_surface;
}

uint32_t VulkanCore::GetGraphicsQueueFamily() const {
    return _graphicsQueueFamily;
}

uint32_t VulkanCore::GetPresentQueueFamily() const {
    return _presentQueueFamily;
}
