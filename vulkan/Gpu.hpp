#pragma once

#include "../pch.hpp"

#include "../utils/macros.hpp"

namespace vulkan {
    struct Gpu {
        struct QueueFamilies {
            std::uint32_t graphicsPresent;
        };

        struct Queues {
            vk::Queue graphicsPresent;
        };

        vk::raii::PhysicalDevice physicalDevice;
        QueueFamilies queueFamilies;

        vk::raii::Device device;
        Queues queues;

        Gpu(vk::raii::PhysicalDevice &&physicalDevice, const vk::SurfaceKHR &surface LIFETIMEBOUND);
    };
}

#include "../utils/ranges.hpp"
#include "../utils/vku.hpp"

inline vulkan::Gpu::Gpu(vk::raii::PhysicalDevice &&physicalDevice_, const vk::SurfaceKHR &surface)
    : physicalDevice { std::move(physicalDevice_) }
    , queueFamilies {
        .graphicsPresent = [&] -> std::uint32_t {
            for (auto [i, props] : ranges::views::enumerate(physicalDevice.getQueueFamilyProperties())) {
                if (vku::contains(props.queueFlags, vk::QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(i, surface)) {
                    return i;
                }
            }

            throw std::runtime_error { "No suitable queue family for both graphics and present operation." };
        }(),
    }
    , device { physicalDevice, vk::StructureChain {
        vk::DeviceCreateInfo {
            {},
            vku::lvalue(vk::DeviceQueueCreateInfo {
                {},
                queueFamilies.graphicsPresent,
                vk::ArrayProxyNoTemporaries<const float> { vku::lvalue(1.f) },
            }),
            {},
            vku::lvalue([this] {
                constexpr std::array requiredExtensions {
                    vk::KHRSwapchainExtensionName,
                };

                std::vector<const char*> result;
                result.reserve(requiredExtensions.size() + 1);

                std::ranges::copy(requiredExtensions, std::back_inserter(result));

                for (const vk::ExtensionProperties &props : physicalDevice.enumerateDeviceExtensionProperties(nullptr)) {
                    if (std::strcmp(props.extensionName, vk::KHRPortabilitySubsetExtensionName) == 0) {
                        result.push_back(vk::KHRPortabilitySubsetExtensionName);
                        break;
                    }
                }

                return result;
            }()),
        },
        vk::PhysicalDeviceVulkan12Features{}
            .setTimelineSemaphore(true),
        vk::PhysicalDeviceVulkan13Features{}
            .setDynamicRendering(true)
            .setSynchronization2(true),
    }.get() }
    , queues {
        .graphicsPresent = *device.getQueue(queueFamilies.graphicsPresent, 0),
    } { }