#include "App.hpp"

#if !defined(__APPLE__) || defined(APPLE_USE_VULKAN)
#define GLFW_INCLUDE_VULKAN
#else
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "utils/macros.hpp"
#include "utils/ranges.hpp"

#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
#define IMGUI_IMPL_METAL_CPP
#include <imgui_impl_metal4.h>

#include "metal/Frame.hpp"
#include "metal/Gpu.hpp"
#include "metal/glfw_metal.hpp"
#else
#include <imgui_impl_vulkan.h>

#include "vulkan/Frame.hpp"
#include "vulkan/Gpu.hpp"
#include "vulkan/Swapchain.hpp"
#endif

struct App::PImpl {
    std::unique_ptr<GLFWwindow, decltype([](GLFWwindow *window) noexcept { glfwDestroyWindow(window); })> window;

#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
    metal::Gpu gpu;

    CA::MetalLayer *layer;

    // ImGui_ImplMetal4_NewFrame() reads MTL4::RenderPassDescriptor's color attachment texture's format and raster sample
    // count to prepare the rendering. But the CA::MetalDrawable texture is not ready at this call for the first frame,
    // therefore the dummy render pass descriptor with texture can be used. The texture is not used in GPU at all.
    NS::SharedPtr<MTL::Texture> dummyTexture;
    NS::SharedPtr<MTL4::RenderPassDescriptor> dummyRenderPassDescriptor;

    NS::SharedPtr<MTL::SharedEvent> sharedEvent;

    boost::container::static_vector<metal::Frame, 2> frames;
#else
    vk::raii::Context context;
    vk::raii::Instance instance;

    vk::raii::SurfaceKHR surface;

    vulkan::Gpu gpu;

    vulkan::Swapchain swapchain;

    std::shared_ptr<vk::raii::Semaphore> sharedSemaphore;

    boost::container::static_vector<vulkan::Frame, 2> frames;
#endif

    explicit PImpl()
        : window { [] {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            return glfwCreateWindow(800, 480, "Model Editor", nullptr, nullptr);
        }() }
    #if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
        , layer { [this] {
            auto result = CA::MetalLayer::layer();
            result->setDevice(gpu.device.get());
            result->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
            result->setFramebufferOnly(true);
            return result;
        }() }
        , dummyTexture { TransferPtr(gpu.device->newTexture([] {
            auto *result = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatBGRA8Unorm, 1, 1, false);
            result->setResourceOptions(MTL::ResourceStorageModeMemoryless);
            return result;
        }())) }
        , dummyRenderPassDescriptor { TransferPtr([this] {
            auto result = MTL4::RenderPassDescriptor::alloc()->init();
            result->colorAttachments()->object(0)->setTexture(dummyTexture.get());
            return result;
        }()) }
        , sharedEvent { TransferPtr(gpu.device->newSharedEvent()) }
        , frames { [this] {
            auto sharedData = metal::Frame::createSharedData(gpu, sharedEvent);

            decltype(frames) result;
            for (auto _ : ranges::views::indices(2)) {
                result.emplace_back(gpu, sharedData);
            }
            return result;
        }() }
    #else
        , instance { context, vk::InstanceCreateInfo {
            vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
            &vku::lvalue(vk::ApplicationInfo {
                "Model Editor", 0,
                nullptr, 0,
                vk::makeApiVersion(0, 1, 3, 0),
            }),
            {},
            vku::lvalue([] {
                std::uint32_t glfwExtensionCount;
                const char** const glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

                std::vector<const char*> result;
                result.reserve(glfwExtensionCount + 1);

                std::copy_n(glfwExtensions, glfwExtensionCount, std::back_inserter(result));

                result.push_back(vk::KHRPortabilityEnumerationExtensionName);

                return result;
            }()),
        } }
        , surface { instance, [this] {
            if (VkSurfaceKHR result; glfwCreateWindowSurface(*instance, window.get(), nullptr, &result) == VK_SUCCESS) {
                return result;
            }

            throw std::runtime_error { "Failed to create Vulkan surface from the GLFW window." };
        }() }
        , gpu { std::move(instance.enumeratePhysicalDevices().at(0) /* TODO */), *surface }
        , swapchain { gpu, *surface, [this] -> vk::Extent2D {
            int width, height;
            glfwGetFramebufferSize(window.get(), &width, &height);
            return { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
        }() }
        , sharedSemaphore { std::make_shared<vk::raii::Semaphore>(gpu.device, vk::StructureChain {
            vk::SemaphoreCreateInfo{},
            vk::SemaphoreTypeCreateInfo { vk::SemaphoreType::eTimeline, 0ULL },
        }.get()) }
        , frames { [this] {
            decltype(frames) result;
            for (auto _ : ranges::views::indices(2)) {
                result.emplace_back(gpu, sharedSemaphore);
            }
            return result;
        }() }
    #endif
    {
#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
        glfwSetWindowMetalLayer(window.get(), layer);

        int framebufferWidth, framebufferHeight;
        glfwGetFramebufferSize(window.get(), &framebufferWidth, &framebufferHeight);

        layer->setDrawableSize({ static_cast<CGFloat>(framebufferWidth), static_cast<CGFloat>(framebufferHeight) });
#endif
    }
};

void App::PImplDeleter::operator()(PImpl *ptr) noexcept {
    delete ptr;
}

App::App()
    : pImpl { new PImpl{} } {
    glfwSetWindowUserPointer(pImpl->window.get(), this);
    glfwSetFramebufferSizeCallback(pImpl->window.get(), [](GLFWwindow *window, int width, int height) {
        while (width == 0 || height == 0) {
            std::this_thread::yield();
            glfwWaitEvents();
        }

        auto &self = *static_cast<App*>(glfwGetWindowUserPointer(window));
#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
        self.pImpl->layer->setDrawableSize({ static_cast<CGFloat>(width), static_cast<CGFloat>(height) });
#else
        self.pImpl->gpu.device.waitIdle();

        const vk::Extent2D extent { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
        self.pImpl->swapchain = vulkan::Swapchain { self.pImpl->gpu, *self.pImpl->surface, extent, const_cast<vk::SwapchainKHR&&>(*std::move(self.pImpl->swapchain)) };
#endif
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#if defined(__APPLE__)
    io.IniFilename = "../../../imgui.ini"; // TODO: use Application Support folder
#endif

#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
    ImGui_ImplGlfw_InitForOther(pImpl->window.get(), true);

    ImGui_ImplMetal4_Init(pImpl->gpu.device.get(), pImpl->gpu.commandQueue.get(), pImpl->frames.size());
#else
    ImGui_ImplGlfw_InitForVulkan(pImpl->window.get(), true);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = vk::makeApiVersion(0, 1, 3, 0);
    info.Instance = *pImpl->instance;
    info.PhysicalDevice = *pImpl->gpu.physicalDevice;
    info.Device = *pImpl->gpu.device;
    info.QueueFamily = pImpl->gpu.queueFamilies.graphicsPresent;
    info.Queue = pImpl->gpu.queues.graphicsPresent;
    info.DescriptorPoolSize = 128;
    info.MinImageCount = 2;
    info.ImageCount = 2;

    constexpr vk::Format pipelineRenderingColorAttachmentFormat = vk::Format::eB8G8R8A8Unorm;
    info.PipelineInfoMain = {
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo {
            {},
            pipelineRenderingColorAttachmentFormat,
        },
        .SwapChainImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };
    info.UseDynamicRendering = true;

    ImGui_ImplVulkan_Init(&info);
#endif
}

void App::run() {
    std::uint64_t frameIndex = 0;
    for (; !glfwWindowShouldClose(pImpl->window.get()); ++frameIndex) {
#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
        DECLARE_AUTORELEASEPOOL;
#endif

        FrameMark;

        // Wait for the previous frame's pending operation to be finished.
        if (frameIndex >= pImpl->frames.size()) {
            ZoneScopedN("Wait for the previous frame GPU execution");

            const std::uint64_t hostWaitValue = frameIndex - pImpl->frames.size() + 1;
#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
            pImpl->sharedEvent->waitUntilSignaledValue(hostWaitValue, ~0ULL);
#else
            std::ignore = pImpl->gpu.device.waitSemaphores({ {}, **pImpl->sharedSemaphore, hostWaitValue }, ~0ULL);
#endif
        }

        const std::uint64_t framesInFlightIndex = frameIndex % pImpl->frames.size();
        auto &frame = pImpl->frames[framesInFlightIndex];

        {
            ZoneScopedN("Process GLFW events");
            glfwPollEvents();
        }

        {
            ZoneScopedN("Render ImGui");

#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
            ImGui_ImplMetal4_NewFrame(pImpl->dummyRenderPassDescriptor.get(), framesInFlightIndex);
#else
            ImGui_ImplVulkan_NewFrame();
#endif
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::DockSpaceOverViewport();

            ImGui::ShowDemoWindow();

            ImGui::Render();
        }

        // --------------------
        // Frame rendering
        // --------------------

#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
        frame.render(pImpl->layer, frameIndex);
#else
        frame.render(pImpl->swapchain, frameIndex);
#endif
    }

    // Wait for all pending operations to be finished.
#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
    pImpl->gpu.commandQueue->wait(pImpl->sharedEvent.get(), frameIndex);
#else
    pImpl->gpu.device.waitIdle();
#endif

    // Cleanup ImGui contexts.
#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
    ImGui_ImplMetal4_Shutdown();
#else
    ImGui_ImplVulkan_Shutdown();
#endif
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}