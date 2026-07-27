#pragma once

#include "../pch.hpp"

#include "../utils/macros.hpp"
#include "../utils/ranges.hpp"
#include "Gpu.hpp"

namespace vulkan {
    struct Swapchain final : vk::raii::SwapchainKHR {
        vk::Extent2D extent;
        std::vector<vk::Image> images;
        std::vector<vk::raii::ImageView> imageViews;
        std::vector<vk::raii::Semaphore> imageReadySemaphores;

        Swapchain(Gpu &gpu LIFETIMEBOUND, const vk::SurfaceKHR &surface LIFETIMEBOUND, vk::Extent2D extent, vk::SwapchainKHR &&oldSwapchain = {});
    };
}

inline vulkan::Swapchain::Swapchain(Gpu &gpu, const vk::SurfaceKHR &surface, vk::Extent2D extent, vk::SwapchainKHR &&oldSwapchain)
    : SwapchainKHR { [&] -> SwapchainKHR {
        const vk::SurfaceCapabilitiesKHR capabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);

        std::uint32_t minImageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount != 0 && minImageCount > capabilities.maxImageCount) {
            minImageCount = capabilities.maxImageCount;
        }

        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return { gpu.device, vk::SwapchainCreateInfoKHR {
            {},
            surface,
            minImageCount,
            vk::Format::eB8G8R8A8Unorm,
            vk::ColorSpaceKHR::eSrgbNonlinear,
            extent,
            1,
            vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive, {},
            vk::SurfaceTransformFlagBitsKHR::eIdentity,
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::PresentModeKHR::eFifo,
            true,
            oldSwapchain,
        } };
    }() }
    , extent { extent }
    , images { getImages() }
    , imageViews { std::ranges::to<std::vector>(std::views::transform(images, [&](vk::Image image) {
        return vk::raii::ImageView { gpu.device, vk::ImageViewCreateInfo {
            {},
            image,
            vk::ImageViewType::e2D,
            vk::Format::eB8G8R8A8Unorm,
            {},
            vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
        } };
    })) }
    , imageReadySemaphores { std::ranges::to<std::vector>(ranges::views::generate_n(images.size(), [&] {
        return vk::raii::Semaphore { gpu.device, vk::SemaphoreCreateInfo{} };
    })) } { }