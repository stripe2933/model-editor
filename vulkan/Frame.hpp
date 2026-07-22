#pragma once

#include "../pch.hpp"

#include "../utils/macros.hpp"
#include "Gpu.hpp"
#include "Swapchain.hpp"

namespace vulkan {
    class Frame {
    public:
        Frame(vulkan::Gpu &gpu LIFETIMEBOUND, std::shared_ptr<vk::raii::Semaphore> sharedSemaphore);

        void render(vulkan::Swapchain &swapchain, std::uint64_t frameIndex);

    private:
        std::reference_wrapper<vulkan::Gpu> gpu;
        std::shared_ptr<vk::raii::Semaphore> sharedSemaphore;

        vk::raii::CommandPool commandPool;
        vk::CommandBuffer commandBuffer;
        vk::raii::Semaphore swapchainImageAcquireSemaphore;
    };
}