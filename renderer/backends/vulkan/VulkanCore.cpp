// Class Header
#include "VulkanCore.h"

// Standard Library Headers
#include <cstring>
#include <iostream>
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

} // namespace

//----------------------------------------------------------------------
// Construction / Destruction

VulkanCore::VulkanCore([[maybe_unused]] GLFWwindow* window) {
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

    std::cout << "[VulkanCore] Instance created successfully." << std::endl;

    // TODO: Create window surface via GLFW
    // TODO: Select physical device with required queue families
    // TODO: Create logical device and retrieve queues

    std::cerr << "[VulkanCore] Remaining initialization not yet implemented." << std::endl;
}

VulkanCore::~VulkanCore() {
    // vk::raii types handle cleanup automatically in reverse declaration order.
    std::cout << "[VulkanCore] Destroyed." << std::endl;
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
