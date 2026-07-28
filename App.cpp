#include "App.hpp"

#include <BS_thread_pool.hpp>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#if !defined(__APPLE__) || defined(APPLE_USE_VULKAN)
#define GLFW_INCLUDE_VULKAN
#else
#define GLFW_INCLUDE_NONE
#endif
#define NFD_NATIVE
#include <nfd_glfw3.h> // Will transitively include <GLFW/glfw3.h>
#include <nfd.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>

#include "gltf/AssetWithDataBuffer.hpp"
#include "utils/algorithm.hpp"
#include "utils/imgui.hpp"
#include "utils/macros.hpp"
#include "utils/ranges.hpp"
#include "utils/TempStringBuffer.hpp"

#ifdef _WIN32
#include <tchar.h>
#endif

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

namespace {
    NFD::Guard guard;

    struct Viewport {
        gltf::AssetWithDataBuffer assetWithDataBuffer;

        ImGuiWindow *window;
    };
}

struct App::PImpl {
    BS::thread_pool<> threadPool;

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

    std::vector<std::shared_ptr<Viewport>> viewports;
    std::weak_ptr<Viewport> focusedViewport;

    struct {
        std::future<gltf::AssetWithDataBuffer> future;
        bool isLoadingPopupOpened;
    } backgroundAssetLoadingContext;

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
    glfwSetDropCallback(pImpl->window.get(), [](GLFWwindow *window, int count, const char **paths) {
        auto &self = *static_cast<App*>(glfwGetWindowUserPointer(window));

        assert(count > 0);
        std::filesystem::path path { reinterpret_cast<const char8_t*>(paths[0]) };

        static const std::filesystem::path gltfExtensions[] = { ".gltf", ".glb" };

        if (is_directory(path)) {
            // If the directory contains glTF file, load it.
            for (const auto &entry : std::filesystem::directory_iterator { path }) {
                const std::filesystem::path &childPath = entry.path();
                if (ranges::any_of(childPath.extension(), gltfExtensions)) {
                    self.loadAsset(childPath);
                    return;
                }
            }
        }
        else if (ranges::any_of(path.extension(), gltfExtensions)) {
            self.loadAsset(std::move(path));
        }
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

void App::loadAsset(std::filesystem::path path) {
    const auto it = std::ranges::find_if(pImpl->viewports, [&path](const auto &viewport) noexcept {
        return viewport->assetWithDataBuffer.path == path;
    });
    if (it != pImpl->viewports.end()) {
        // Same asset already loaded. Focus the viewport window associated to that.
        assert((*it)->window);
        ImGui::FocusWindow((*it)->window);
    }
    else if (auto &context = pImpl->backgroundAssetLoadingContext; !context.future.valid() /* Allow the request only if no other asset is loading */) {
        context = {
            .future = pImpl->threadPool.submit_task([MOVE_CAP(path)] mutable {
                ZoneScopedN("Load glTF Asset");
                return gltf::AssetWithDataBuffer { std::move(path) };
            }),
            .isLoadingPopupOpened = false,
        };
    }
}

void App::openAssetWithDialog() {
    constexpr std::array filters {
        nfdfilteritem_t { TEXT("glTF 2.0 Asset"), TEXT("gltf,glb") },
    };

    NFD::UniquePathN path;

    nfdwindowhandle_t windowHandle{};
    NFD_GetNativeWindowFromGLFWWindow(pImpl->window.get(), &windowHandle);

    if (NFD::OpenDialog(path, filters.data(), filters.size(), nullptr, windowHandle) == NFD_OKAY) {
        loadAsset(path.get());
    }
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
            ZoneScopedN("Process background tasks");

            if (auto &future = pImpl->backgroundAssetLoadingContext.future; future.valid()) {
                if (future.wait_for(std::chrono::seconds{}) == std::future_status::ready) {
                    pImpl->viewports.push_back(std::make_shared<Viewport>(future.get(), nullptr));
                }
            }
        }

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

            const ImGuiID dockSpaceId = ImGui::DockSpaceOverViewport();

            // ----- Menu bar -----

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_O);
                    if (ImGui::MenuItem("Open...", "Ctrl+O" /* TODO: use ⌘ in macOS */)) {
                        openAssetWithDialog();
                    }

                    ImGui::Separator();

                    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_W);
                    if (ImGui::MenuItem("Close Asset", "Ctrl+W" /* TODO: use ⌘ in macOS */, false, !pImpl->focusedViewport.expired())) {
                        if (const auto &viewport = pImpl->focusedViewport.lock()) {
                            pImpl->viewports.erase(std::ranges::find(pImpl->viewports, viewport));
                            pImpl->focusedViewport.reset();
                        }
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMainMenuBar();
            }

            // ----- Global shortcut processing -----

            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
                openAssetWithDialog();
            }

            if (auto &context = pImpl->backgroundAssetLoadingContext; context.future.valid()) {
                if (!context.isLoadingPopupOpened) {
                    ImGui::OpenPopup("asset-loading-indicator-popup");
                    context.isLoadingPopupOpened = true;
                }

                if (ImGui::BeginPopupModal("asset-loading-indicator-popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
                    imgui::widget::Spinner("##asset-loading-indicator", 24.f, 4.f, ImGui::GetColorU32(ImGuiCol_ButtonActive));
                    ImGui::EndPopup();
                }
            }

            // ----- Viewport windows -----

            const ImGuiID centralDockSpaceId = ImGui::DockBuilderGetCentralNode(dockSpaceId)->ID;
            for (auto it = pImpl->viewports.begin(); it != pImpl->viewports.end(); ) {
                auto &viewport = *it;
                bool windowOpened = true;

                // When the viewport window is created at the first time, dock it to the central dock space.
                ImGui::SetNextWindowDockID(centralDockSpaceId, ImGuiCond_Appearing);

                // Different assets may have the same basename, so &viewport is used for the persistent window ID.
                if (ImGui::Begin(tempStringBuffer.write("{}###{}", viewport->assetWithDataBuffer.path.filename(), fmt::ptr(&viewport)).view().c_str(), &windowOpened, ImGuiWindowFlags_NoSavedSettings) && windowOpened) {
                    if (!viewport->window) {
                        viewport->window = ImGui::GetCurrentWindow();
                    }

                    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, ImGuiInputFlags_RouteFocused)) {
                        windowOpened = false;
                    }
                    else {
                        if (ImGui::IsWindowFocused()) {
                            pImpl->focusedViewport = viewport;
                        }
                    }
                }
                ImGui::End();

                if (windowOpened) {
                    ++it;
                }
                else {
                    if (viewport == pImpl->focusedViewport.lock()) {
                        pImpl->focusedViewport.reset();
                    }

                    it = pImpl->viewports.erase(it);
                }
            }

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