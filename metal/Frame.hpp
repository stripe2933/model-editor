#pragma once

#include "../pch.hpp"

#include "../utils/macros.hpp"
#include "Gpu.hpp"

namespace metal {
    class Frame {
    public:
        struct SharedData;

        Frame(Gpu &gpu LIFETIMEBOUND, std::shared_ptr<SharedData> sharedData);

        void render(CA::MetalLayer *layer, std::uint64_t frameIndex);

        [[nodiscard]] static std::shared_ptr<SharedData> createSharedData(Gpu &gpu LIFETIMEBOUND, NS::SharedPtr<MTL::SharedEvent> sharedEvent);

    private:
        std::reference_wrapper<Gpu> gpu;
        std::shared_ptr<SharedData> sharedData;

        NS::SharedPtr<MTL4::RenderPassDescriptor> renderPassDescriptor;

        NS::SharedPtr<MTL4::CommandAllocator> commandAllocator;
        NS::SharedPtr<MTL4::CommandBuffer> commandBuffer;
    };
}