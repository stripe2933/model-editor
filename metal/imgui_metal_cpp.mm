#include <imgui_impl_metal4.h>

#include <Metal/Metal.hpp>

bool ImGui_ImplMetal4_Init(MTL::Device* device, MTL4::CommandQueue* commandQueue, int framesInFlight)
{
    return ImGui_ImplMetal4_Init((__bridge id<MTLDevice>)(device),(__bridge id<MTL4CommandQueue>)(commandQueue), framesInFlight);
}

void ImGui_ImplMetal4_NewFrame(MTL4::RenderPassDescriptor* renderPassDescriptor, int frameInFlightIndex)
{
    ImGui_ImplMetal4_NewFrame((__bridge MTL4RenderPassDescriptor*)(renderPassDescriptor), frameInFlightIndex);
}

void ImGui_ImplMetal4_RenderDrawData(ImDrawData* draw_data,
                                    MTL4::CommandBuffer* commandBuffer,
                                    MTL4::RenderCommandEncoder* commandEncoder)
{
    ImGui_ImplMetal4_RenderDrawData(draw_data,
                                   (__bridge id<MTL4CommandBuffer>)(commandBuffer),
                                   (__bridge id<MTL4RenderCommandEncoder>)(commandEncoder));
}