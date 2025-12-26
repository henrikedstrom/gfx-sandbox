// Class Header
#include "VulkanSwapchain.h"

// Standard Library Headers
#include <algorithm>
#include <limits>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Project Headers
#include "VulkanCore.h"

//----------------------------------------------------------------------
// Internal Helper Functions

namespace {

// Holds surface capabilities, formats, and present modes.
// Used to choose swapchain settings from available options.
struct SwapchainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

SwapchainSupportDetails QuerySwapchainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
    SwapchainSupportDetails details;
    details.capabilities = device.getSurfaceCapabilitiesKHR(surface);
    details.formats = device.getSurfaceFormatsKHR(surface);
    details.presentModes = device.getSurfacePresentModesKHR(surface);
    return details;
}

// Choose the best surface format.
// Prefer SRGB color space with B8G8R8A8 format for best color accuracy.
vk::SurfaceFormatKHR ChooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    // Fallback to the first available format
    return formats[0];
}

const char* PresentModeToString(vk::PresentModeKHR mode) {
    switch (mode) {
    case vk::PresentModeKHR::eImmediate:
        return "Immediate";
    case vk::PresentModeKHR::eMailbox:
        return "Mailbox";
    case vk::PresentModeKHR::eFifo:
        return "FIFO";
    case vk::PresentModeKHR::eFifoRelaxed:
        return "FIFO Relaxed";
    case vk::PresentModeKHR::eSharedDemandRefresh:
        return "Shared Demand Refresh";
    case vk::PresentModeKHR::eSharedContinuousRefresh:
        return "Shared Continuous Refresh";
    default:
        return "Unknown";
    }
}

// Choose the best present mode.
// Uses preferred mode from VulkanConfig.h, falls back to FIFO (always available).
vk::PresentModeKHR ChoosePresentMode(const std::vector<vk::PresentModeKHR>& presentModes) {
    for (const auto& mode : presentModes) {
        if (mode == vkbackend::kPreferredPresentMode) {
            VK_LOG_INFO("Present mode: {}", PresentModeToString(mode));
            return mode;
        }
    }
    VK_LOG_INFO("Present mode: FIFO (fallback)");
    return vk::PresentModeKHR::eFifo;
}

// Choose the swap extent (resolution of swapchain images).
// Handles high-DPI displays by querying the framebuffer size.
vk::Extent2D ChooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) {
    // If currentExtent is not the special value, use it directly
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    // Otherwise, query the actual framebuffer size (handles HiDPI)
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    vk::Extent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    // Clamp to the allowed range
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);

    return extent;
}

} // namespace

//----------------------------------------------------------------------
// Construction / Destruction

VulkanSwapchain::VulkanSwapchain(const VulkanCore& core, GLFWwindow* window) {
    CreateSwapchain(core, window);
    CreateImageViews(core.GetRaiiDevice());

    VK_LOG_INFO("Swapchain created: {}x{}, {} images", _extent.width, _extent.height,
                static_cast<uint32_t>(_images.size()));
}

VulkanSwapchain::~VulkanSwapchain() {
    VK_LOG_INFO("Swapchain destroyed.");
}

//----------------------------------------------------------------------
// Swapchain Recreation

void VulkanSwapchain::Recreate(const VulkanCore& core, GLFWwindow* window) {
    // Wait for device to finish any ongoing operations
    core.GetDevice().waitIdle();

    // Clear old image views (RAII handles destruction)
    _imageViews.clear();
    _images.clear();

    // Create new swapchain and image views
    CreateSwapchain(core, window);
    CreateImageViews(core.GetRaiiDevice());

    VK_LOG_INFO("Swapchain recreated: {}x{}, {} images", _extent.width, _extent.height,
                static_cast<uint32_t>(_images.size()));
}

//----------------------------------------------------------------------
// Internal Creation Methods

void VulkanSwapchain::CreateSwapchain(const VulkanCore& core, GLFWwindow* window) {
    // Query swapchain support details
    SwapchainSupportDetails support =
        QuerySwapchainSupport(core.GetPhysicalDevice(), core.GetSurface());

    // Choose optimal settings
    vk::SurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
    vk::PresentModeKHR presentMode = ChoosePresentMode(support.presentModes);
    vk::Extent2D extent = ChooseExtent(support.capabilities, window);

    // Store format and extent for later use
    _imageFormat = surfaceFormat.format;
    _extent = extent;

    // Request one more image than the minimum to avoid waiting on the driver
    uint32_t imageCount = support.capabilities.minImageCount + 1;

    // Don't exceed the maximum (0 means no limit)
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    // Build swapchain create info
    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = core.GetSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1; // Always 1 unless stereoscopic 3D
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    // Handle queue family ownership
    uint32_t graphicsFamily = core.GetGraphicsQueueFamily();
    uint32_t presentFamily = core.GetPresentQueueFamily();
    uint32_t queueFamilyIndices[] = {graphicsFamily, presentFamily};

    if (graphicsFamily != presentFamily) {
        // Images are shared between queue families (simpler, slightly less performant)
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        // Images are exclusive to one queue family (optimal)
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE; // Don't render pixels obscured by other windows

    // Pass old swapchain for resource reuse during recreation
    createInfo.oldSwapchain = *_swapchain;

    // Create the swapchain using RAII device
    _swapchain = vk::raii::SwapchainKHR(core.GetRaiiDevice(), createInfo);

    // Retrieve swapchain images (owned by the swapchain, not RAII managed)
    _images = _swapchain.getImages();
}

void VulkanSwapchain::CreateImageViews(const vk::raii::Device& device) {
    _imageViews.clear();
    _imageViews.reserve(_images.size());

    for (const auto& image : _images) {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.image = image;
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = _imageFormat;

        // Standard component mapping (identity)
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;

        // Color attachment, single mip level, single array layer
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        _imageViews.emplace_back(device, createInfo);
    }
}

//----------------------------------------------------------------------
// Accessors

vk::SwapchainKHR VulkanSwapchain::GetSwapchain() const {
    return *_swapchain;
}

vk::Format VulkanSwapchain::GetImageFormat() const {
    return _imageFormat;
}

vk::Extent2D VulkanSwapchain::GetExtent() const {
    return _extent;
}

const std::vector<vk::Image>& VulkanSwapchain::GetImages() const {
    return _images;
}

const std::vector<vk::raii::ImageView>& VulkanSwapchain::GetImageViews() const {
    return _imageViews;
}

uint32_t VulkanSwapchain::GetImageCount() const {
    return static_cast<uint32_t>(_images.size());
}
