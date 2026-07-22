#include "Frame.hpp"

#define IMGUI_IMPL_METAL_CPP
#include <imgui_impl_metal4.h>

struct metal::Frame::SharedData {
    NS::SharedPtr<MTL::Fence> fence;
    NS::SharedPtr<MTL::SharedEvent> sharedEvent;
};

metal::Frame::Frame(Gpu &gpu, std::shared_ptr<SharedData> sharedData_)
    : gpu { gpu }
    , sharedData { std::move(sharedData_) }
    , renderPassDescriptor { TransferPtr([] {
        auto result = MTL4::RenderPassDescriptor::alloc()->init();

        MTL::RenderPassColorAttachmentDescriptor* const attachment = result->colorAttachments()->object(0);
        attachment->setLoadAction(MTL::LoadActionDontCare);
        attachment->setStoreAction(MTL::StoreActionStore);

        return result;
    }()) }
    , commandAllocator { TransferPtr(gpu.device->newCommandAllocator()) }
    , commandBuffer { TransferPtr(gpu.device->newCommandBuffer()) } { }

std::shared_ptr<metal::Frame::SharedData> metal::Frame::createSharedData(Gpu &gpu, NS::SharedPtr<MTL::SharedEvent> sharedEvent) {
    return std::make_shared<SharedData>(
        TransferPtr(gpu.device->newFence()),
        std::move(sharedEvent));
}

void metal::Frame::render(CA::MetalLayer *layer, std::uint64_t frameIndex) {
    ZoneScoped;

    CA::MetalDrawable* const drawable = [&] {
        ZoneScopedN("Get next drawable");
        return layer->nextDrawable();
    }();

    MTL::Texture* const texture = drawable->texture();
    renderPassDescriptor->colorAttachments()->object(0)->setTexture(texture);
    renderPassDescriptor->setRenderTargetWidth(texture->width());
    renderPassDescriptor->setRenderTargetHeight(texture->height());

    {
        ZoneScopedN("Record frame command buffer");

        commandAllocator->reset();
        commandBuffer->beginCommandBuffer(commandAllocator.get());

        MTL4::RenderCommandEncoder* const renderCommandEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor.get());
        renderCommandEncoder->waitForFence(sharedData->fence.get(), MTL::StageFragment);
        ImGui_ImplMetal4_RenderDrawData(ImGui::GetDrawData(), commandBuffer.get(), renderCommandEncoder);
        renderCommandEncoder->updateFence(sharedData->fence.get(), MTL::StageFragment);
        renderCommandEncoder->endEncoding();

        commandBuffer->endCommandBuffer();
    }

    {
        ZoneScopedN("Commit frame command buffer");

        gpu.get().commandQueue->wait(drawable);

        MTL4::CommandBuffer* const commandBuffers[] = { commandBuffer.get() };
        gpu.get().commandQueue->commit(commandBuffers, std::size(commandBuffers));

        gpu.get().commandQueue->signalEvent(sharedData->sharedEvent.get(), frameIndex + 1);
        gpu.get().commandQueue->signalDrawable(drawable);
    }

    {
        ZoneScopedN("Present the drawable");
        drawable->present();
    }
}