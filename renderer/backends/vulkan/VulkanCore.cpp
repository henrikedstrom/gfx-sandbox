// Class Header
#include "VulkanCore.h"

// Standard Library Headers
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
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
//
// Enabled by default in Debug builds. TODO: Make this a cvar later.

#if defined(NDEBUG)
bool enableValidationLayers = false;
#else
bool enableValidationLayers = true;
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
    // Get extensions required by GLFW for surface creation.
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    // Add debug utils extension for validation layer messages.
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Add portability enumeration for MoltenVK on macOS.
#if defined(__APPLE__)
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    return extensions;
}

//----------------------------------------------------------------------
// Debug Messenger Callback

VkBool32 DebugMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
                                const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                [[maybe_unused]] void* pUserData) {
    if (messageSeverity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        Log::Error(Log::Vulkan, "Validation: {}", pCallbackData->pMessage);
    } else if (messageSeverity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        Log::Warning(Log::Vulkan, "Validation: {}", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

vk::DebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo() {
    vk::DebugUtilsMessengerCreateInfoEXT createInfo;
    createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                 vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    createInfo.pfnUserCallback = DebugMessengerCallback;
    return createInfo;
}

//----------------------------------------------------------------------
// Device Extensions

std::vector<const char*> GetRequiredDeviceExtensions() {
    std::vector<const char*> extensions;

    // Swapchain extension is required for presentation.
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Portability subset extension for MoltenVK on macOS.
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
        // Check for graphics support.
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }

        // Check for present support.
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

    // Check for swapchain support.
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

    // Prefer discrete GPU if available.
    for (const auto& device : devices) {
        if (IsDeviceSuitable(device, surface)) {
            auto properties = device.getProperties();
            if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                Log::Info(Log::Vulkan, "Selected discrete GPU: {}", properties.deviceName.data());
                return device;
            }
        }
    }

    // Fallback to any suitable device.
    for (const auto& device : devices) {
        if (IsDeviceSuitable(device, surface)) {
            auto properties = device.getProperties();
            Log::Info(Log::Vulkan, "Selected device: {}", properties.deviceName.data());
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

    //----------------------------------
    // Verify Vulkan version support.

    constexpr uint32_t kRequiredVersion = VK_API_VERSION_1_3;

    uint32_t supportedVersion = 0;
    if (vkEnumerateInstanceVersion(&supportedVersion) != VK_SUCCESS) {
        // vkEnumerateInstanceVersion is Vulkan 1.1+; if it fails, assume 1.0
        supportedVersion = VK_API_VERSION_1_0;
    }

    if (supportedVersion < kRequiredVersion) {
        throw std::runtime_error("Vulkan version " +
                                 std::to_string(VK_API_VERSION_MAJOR(kRequiredVersion)) + "." +
                                 std::to_string(VK_API_VERSION_MINOR(kRequiredVersion)) +
                                 " required, but driver only supports " +
                                 std::to_string(VK_API_VERSION_MAJOR(supportedVersion)) + "." +
                                 std::to_string(VK_API_VERSION_MINOR(supportedVersion)) + ".");
    }

    //----------------------------------
    // Create the Vulkan instance.

    if (enableValidationLayers && !CheckValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested but not available.");
    }

    vk::ApplicationInfo appInfo{};
    appInfo.apiVersion = kRequiredVersion;

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

    // Chain debug messenger for instance creation/destruction messages.
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        debugCreateInfo = MakeDebugMessengerCreateInfo();
        createInfo.setEnabledLayerCount(static_cast<uint32_t>(kValidationLayers.size()));
        createInfo.setPpEnabledLayerNames(kValidationLayers.data());
        createInfo.setPNext(&debugCreateInfo);
    }

    _instance = vk::raii::Instance(_context, createInfo);

    // Load instance-level functions into the dispatcher.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*_instance);

    // Create debug messenger.
    if (enableValidationLayers) {
        _debugMessenger =
            vk::raii::DebugUtilsMessengerEXT(_instance, MakeDebugMessengerCreateInfo());
    }

    Log::Debug(Log::Vulkan, "Instance created successfully");

    //----------------------------------
    // Create window surface via GLFW.

    VkSurfaceKHR surfaceHandle = VK_NULL_HANDLE;
    vk::Result result =
        vk::Result(glfwCreateWindowSurface(static_cast<VkInstance>(*_instance), window,
                                           nullptr, // Use default allocator
                                           &surfaceHandle));

    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create GLFW window surface.");
    }

    // Wrap the surface handle in a RAII wrapper.
    _surface = vk::raii::SurfaceKHR(_instance, vk::SurfaceKHR(surfaceHandle));

    //----------------------------------
    // Select physical device.

    vk::PhysicalDevice selectedDevice = SelectPhysicalDevice(*_instance, *_surface);
    _physicalDevice =
        vk::raii::PhysicalDevice(_instance, static_cast<VkPhysicalDevice>(selectedDevice));

    // Find queue family indices.
    QueueFamilyIndices queueIndices = FindQueueFamilies(*_physicalDevice, *_surface);

    if (!queueIndices.isComplete()) {
        throw std::runtime_error("Failed to find required queue families.");
    }

    _graphicsQueueFamily = queueIndices.graphicsFamily.value();
    _presentQueueFamily = queueIndices.presentFamily.value();

    Log::Debug(Log::Vulkan, "Physical device selected: Graphics queue {}, Present queue {}",
               _graphicsQueueFamily, _presentQueueFamily);

    //----------------------------------
    // Create logical device and retrieve queues.

    // Collect unique queue families (graphics and present may be the same).
    std::set<uint32_t> uniqueQueueFamilies = {_graphicsQueueFamily, _presentQueueFamily};

    // Create QueueCreateInfos for each unique queue family.
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Specify device features to enable.
    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE; // Enable anisotropic filtering for textures

    // Query Vulkan 1.3 features to check availability.
    auto availableFeatures =
        _physicalDevice
            .getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    auto& vulkan13Features = availableFeatures.get<vk::PhysicalDeviceVulkan13Features>();

    // Enable shaderDemoteToHelperInvocation if supported (required for 'discard' in shaders).
    vk::PhysicalDeviceVulkan13Features enabledVulkan13Features{};
    if (vulkan13Features.shaderDemoteToHelperInvocation) {
        enabledVulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;
        Log::Debug(Log::Vulkan, "Enabling shaderDemoteToHelperInvocation feature");
    } else {
        Log::Warning(Log::Vulkan, "shaderDemoteToHelperInvocation not supported by device");
    }

    // Get required device extensions.
    auto deviceExtensions = GetRequiredDeviceExtensions();

    // Create device create info with feature chain.
    vk::DeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCreateInfo.setPNext(&enabledVulkan13Features); // Chain Vulkan 1.3 features

    // Create the logical device.
    _device = vk::raii::Device(_physicalDevice, deviceCreateInfo);

    // Load device-level functions into the dispatcher.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*_device);

    // Retrieve queue handles from the device.
    _graphicsQueue = vk::raii::Queue(_device, _graphicsQueueFamily, 0);
    _presentQueue = vk::raii::Queue(_device, _presentQueueFamily, 0);

    Log::Debug(Log::Vulkan, "Logical device and queues created successfully");
}

VulkanCore::~VulkanCore() {
    // vk::raii types handle cleanup automatically in reverse declaration order.
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

const vk::raii::Device& VulkanCore::GetRaiiDevice() const {
    return _device;
}

const vk::raii::PhysicalDevice& VulkanCore::GetRaiiPhysicalDevice() const {
    return _physicalDevice;
}

uint32_t VulkanCore::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    auto memProperties = _physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanCore::CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                              vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer,
                              vk::raii::DeviceMemory& bufferMemory) const {
    if (size == 0) {
        Log::Error(Log::Vulkan, "CreateBuffer called with size 0");
        throw std::runtime_error("CreateBuffer: size must be greater than 0");
    }

    // Create the buffer.
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    try {
        buffer = _device.createBuffer(bufferInfo);
    } catch (const vk::SystemError& e) {
        Log::Error(Log::Vulkan, "Failed to create buffer ({} bytes): {}", size, e.what());
        throw;
    }

    // Allocate memory for the buffer.
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    try {
        bufferMemory = _device.allocateMemory(allocInfo);
    } catch (const vk::SystemError& e) {
        Log::Error(Log::Vulkan, "Failed to allocate buffer memory ({} bytes): {}",
                   memRequirements.size, e.what());
        throw;
    }

    buffer.bindMemory(*bufferMemory, 0);
}
