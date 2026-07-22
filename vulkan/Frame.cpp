#include "Frame.hpp"

#include <imgui_impl_vulkan.h>

#include "../utils/vku.hpp"

vulkan::Frame::Frame(Gpu &gpu, std::shared_ptr<vk::raii::Semaphore> sharedSemaphore_)
    : gpu { gpu }
    , sharedSemaphore { std::move(sharedSemaphore_) }
    , commandPool { gpu.device, vk::CommandPoolCreateInfo {
        {},
        gpu.queueFamilies.graphicsPresent,
    } }
    , commandBuffer { (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo {
        *commandPool,
        vk::CommandBufferLevel::ePrimary,
        1,
    }, *gpu.device.getDispatcher())[0] }
    , swapchainImageAcquireSemaphore { gpu.device, vk::SemaphoreCreateInfo{} } { }

void vulkan::Frame::render(Swapchain &swapchain, std::uint64_t frameIndex) {
    ZoneScoped;

    std::uint32_t swapchainImageIndex;
    try {
        ZoneScopedN("Acquire swapchain image");
        swapchainImageIndex = swapchain.acquireNextImage(~0ULL, *swapchainImageAcquireSemaphore, {}).value;
    }
    catch (const vk::OutOfDateKHRError&) {
        return;
    }

    {
        ZoneScopedN("Record frame command buffer");

        commandPool.reset({});

        commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, *gpu.get().device.getDispatcher());

        commandBuffer.pipelineBarrier2({
            {}, {}, {},
            vku::lvalue(vk::ImageMemoryBarrier2 {
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, {},
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                swapchain.images[swapchainImageIndex], vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
            }),
        }, *gpu.get().device.getDispatcher());

        commandBuffer.beginRendering({
            {},
            vk::Rect2D { {}, swapchain.extent },
            1,
            {},
            vku::lvalue(vk::RenderingAttachmentInfo {
                swapchain.imageViews[swapchainImageIndex], vk::ImageLayout::eColorAttachmentOptimal,
                {}, {}, {},
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
            }),
        }, *gpu.get().device.getDispatcher());

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

        commandBuffer.endRendering(*gpu.get().device.getDispatcher());

        commandBuffer.pipelineBarrier2({
            {}, {}, {},
            vku::lvalue(vk::ImageMemoryBarrier2 {
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, {},
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                swapchain.images[swapchainImageIndex], vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
            }),
        }, *gpu.get().device.getDispatcher());

        commandBuffer.end(*gpu.get().device.getDispatcher());
    }

    {
        ZoneScopedN("Submit frame command buffer");

        gpu.get().queues.graphicsPresent.submit2(vk::SubmitInfo2 {
            {},
            vku::lvalue({
                vk::SemaphoreSubmitInfo { *swapchainImageAcquireSemaphore, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput },
                vk::SemaphoreSubmitInfo { **sharedSemaphore, frameIndex, vk::PipelineStageFlagBits2::eColorAttachmentOutput },
            }),
            vku::lvalue(vk::CommandBufferSubmitInfo {
                commandBuffer,
            }),
            vku::lvalue({
                vk::SemaphoreSubmitInfo { *swapchain.imageReadySemaphores[swapchainImageIndex], {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput },
                vk::SemaphoreSubmitInfo { **sharedSemaphore, frameIndex + 1, vk::PipelineStageFlagBits2::eColorAttachmentOutput },
            }),
        }, {}, *gpu.get().device.getDispatcher());
    }

    try {
        ZoneScopedN("Present the swapchain image");

        std::ignore = gpu.get().queues.graphicsPresent.presentKHR(vk::PresentInfoKHR {
            *swapchain.imageReadySemaphores[swapchainImageIndex],
            *swapchain,
            swapchainImageIndex,
        }, *gpu.get().device.getDispatcher());
    }
    catch (const vk::OutOfDateKHRError&) { }
}