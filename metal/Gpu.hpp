#pragma once

#include "../pch.hpp"

namespace metal {
    struct Gpu {
        NS::SharedPtr<MTL::Device> device;

        NS::SharedPtr<MTL::Library> defaultLibrary;

        NS::SharedPtr<MTL4::CommandQueue> commandQueue;

        MTL::CaptureManager *captureManager;

        explicit Gpu();
    };
}

inline metal::Gpu::Gpu()
    : device { TransferPtr(MTL::CreateSystemDefaultDevice()) }
    , defaultLibrary { TransferPtr(device->newDefaultLibrary()) }
    , commandQueue { TransferPtr(device->newMTL4CommandQueue()) }
    , captureManager { MTL::CaptureManager::sharedCaptureManager() } { }
