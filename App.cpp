#include "App.hpp"

#include <BS_thread_pool.hpp>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>

#include "gltf/AssetWithDataBuffer.hpp"
#include "utils/algorithm.hpp"
#include "utils/functional.hpp"
#include "utils/imgui.hpp"
#include "utils/macros.hpp"
#include "utils/ranges.hpp"
#include "utils/TempStringBuffer.hpp"
#include "utils/utf8.hpp"

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

    struct GuiState {
        struct SettingsHandler final : ImGuiSettingsHandler {
            explicit SettingsHandler() {
                enum class Section : std::intptr_t {
                    RecentAssets = 1, // Starts from 1 to prevent void* conversion being nullptr
                };

                TypeName = "UserData";
                TypeHash = ImHashStr("UserData");
                ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char *name) -> void* {
                    if (std::strcmp(name, "RecentAssets") == 0) {
                        return reinterpret_cast<void*>(std::to_underlying(Section::RecentAssets));
                    }

                    return nullptr;
                };
                ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler *handler, void *entry, const char *line) {
                    auto &userData = *static_cast<GuiState*>(handler->UserData);

                    switch (static_cast<Section>(reinterpret_cast<std::underlying_type_t<Section>>(entry))) {
                        case Section::RecentAssets:
                            if (line[0] != '\0') {
                                userData.recentAssets.emplace_back(line, false);
                            }
                            break;
                        default:
                            break;
                    }
                };
                WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler *handler, ImGuiTextBuffer *outBuf) {
                    auto &userData = *static_cast<const GuiState*>(handler->UserData);

                    const auto appendToOutBuf = [outBuf](std::string_view text) {
                        outBuf->append(text.data(), text.data() + text.size());
                    };

                    if (!userData.recentAssets.empty()) {
                        appendToOutBuf("[UserData][RecentAssets]\n");
                        for (std::string_view path : std::views::keys(userData.recentAssets)) {
                            appendToOutBuf(path);
                            appendToOutBuf("\n");
                        }
                        appendToOutBuf("\n");
                    }
                };
            }
        };

        std::vector<std::pair<std::string, bool /* loaded */>> recentAssets;

        std::string recentAssetSearchText;
        std::vector<std::pair<std::size_t /* index in recentAssets */, boost::container::small_vector<std::size_t, 4> /* occurrence offsets */>> recentAssetSearchResult;
        bool recentAssetSearchResultInvalidated; // should be set to true when either recentAssets or recentAssetSearchText changed
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

    GuiState guiState;
    GuiState::SettingsHandler guiStateSettingsHandler;

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
        , guiState {
            .recentAssetSearchResultInvalidated = false,
        } {
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
        std::filesystem::path path = utils::toPath(ranges::views::cast<char8_t>(std::string_view { paths[0] }));

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
    io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/AppleSDGothicNeo.ttc", 16.f);
    io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/Apple Color Emoji.ttc", 16.f);

    pImpl->guiStateSettingsHandler.UserData = &pImpl->guiState;
    ImGui::AddSettingsHandler(&pImpl->guiStateSettingsHandler);

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

        // Update recentAssets
        std::string pathString = utils::toUTF8NFCString(path);
        const auto recentAssetIt = std::ranges::find(pImpl->guiState.recentAssets, pathString, LIFT(get<0>));
        if (recentAssetIt == pImpl->guiState.recentAssets.end()) {
            pImpl->guiState.recentAssets.emplace_back(std::move(pathString), true);
        }
        else {
            // Mark the asset as loaded
            recentAssetIt->second = true;

            // Move it to the end
            std::rotate(recentAssetIt, std::next(recentAssetIt, 1), pImpl->guiState.recentAssets.end());
        }

        pImpl->guiState.recentAssetSearchResultInvalidated = true;
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
                    const auto &viewport = pImpl->viewports.emplace_back(std::make_shared<Viewport>(future.get(), nullptr));

                    // Update recentAssets
                    std::string pathString = utils::toUTF8NFCString(viewport->assetWithDataBuffer.path);
                    const auto it = std::ranges::find(pImpl->guiState.recentAssets, pathString, LIFT(get<0>));
                    if (it == pImpl->guiState.recentAssets.end()) {
                        pImpl->guiState.recentAssets.emplace_back(std::move(pathString), true);
                    }
                    else {
                        // Mark the asset as loaded
                        it->second = true;

                        // Move it to the end
                        std::rotate(it, std::next(it, 1), pImpl->guiState.recentAssets.end());
                    }

                    pImpl->guiState.recentAssetSearchResultInvalidated = true;
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

                    if (ImGui::BeginMenu("Recent Asset")) {
                        if (pImpl->guiState.recentAssets.empty()) {
                            ImGui::TextDisabled("No recent assets");
                        }
                        else {
                            const std::size_t searchTextSizeBefore = pImpl->guiState.recentAssetSearchText.size();
                            bool insertedAtBeginOrEnd = false;
                            auto userData = std::tie(searchTextSizeBefore, insertedAtBeginOrEnd);

                            const bool searchTextChanged = imgui::widget::InputTextWithHint(
                                "##recent-asset-search-text",
                                "Search...",
                                &pImpl->guiState.recentAssetSearchText,
                                ImGuiInputTextFlags_CallbackEdit,
                                [](ImGuiInputTextCallbackData *data) {
                                    const auto &[searchTextSizeBefore, insertedAtBeginOrEnd] = *static_cast<decltype(userData)*>(data->UserData);
                                    insertedAtBeginOrEnd = data->BufTextLen > searchTextSizeBefore // Text is inserted
                                        && (data->CursorPos == data->BufTextLen // at the end of the text, or
                                            || searchTextSizeBefore + data->CursorPos == data->BufTextLen); // at the beginning of the text.
                                    return 0;
                                }, &userData);

                            if (pImpl->guiState.recentAssetSearchResultInvalidated || searchTextChanged) {
                                if (pImpl->guiState.recentAssetSearchText.empty()) {
                                    pImpl->guiState.recentAssetSearchResult.clear();
                                }
                                else {
                                    namespace sz = ashvardanian::stringzilla;
                                    const sz::utf8_uncased_needle<> needle { pImpl->guiState.recentAssetSearchText };

                                    // If the newly entered search text is an extension of the previous search text (i.e.
                                    // the user is typing more characters to narrow down the search), we can update the
                                    // occurrence offsets in-place.
                                    // Otherwise, we need to recalculate the occurrence offsets for all recent asset paths.
                                    if (!pImpl->guiState.recentAssetSearchResultInvalidated && (searchTextSizeBefore != 0 && insertedAtBeginOrEnd)) {
                                        // Update the occurrence offsets.
                                        for (auto &[i, offsets] : pImpl->guiState.recentAssetSearchResult) {
                                            offsets.clear();
                                            utils::getCaseInsensitiveOccurrenceOffsets(sz::string_view { pImpl->guiState.recentAssets[i].first }, needle, std::back_inserter(offsets));
                                        }

                                        // Remove paths that no longer match the search text.
                                        std::erase_if(pImpl->guiState.recentAssetSearchResult, utils::decomposer([](std::size_t, const auto &occurrenceOffsets) noexcept {
                                            return occurrenceOffsets.empty();
                                        }));
                                    }
                                    else {
                                        // Copy paths that match the search text, along with the occurrence offset, to the
                                        // filtered path vector.
                                        pImpl->guiState.recentAssetSearchResult.clear();
                                        for (auto [i, path] : ranges::views::enumerate(std::views::keys(pImpl->guiState.recentAssets))) {
                                            boost::container::small_vector<std::size_t, 4> offsets;
                                            utils::getCaseInsensitiveOccurrenceOffsets(sz::string_view { path }, needle, std::back_inserter(offsets));
                                            if (!offsets.empty()) {
                                                pImpl->guiState.recentAssetSearchResult.emplace_back(i, std::move(offsets));
                                            }
                                        }
                                    }
                                }

                                pImpl->guiState.recentAssetSearchResultInvalidated = false;
                            }

                            ImGui::Separator();

                            if (pImpl->guiState.recentAssetSearchText.empty()) {
                                for (auto it = pImpl->guiState.recentAssets.rbegin(); it != pImpl->guiState.recentAssets.rend();) {
                                    const std::size_t i = std::distance(pImpl->guiState.recentAssets.begin(), it.base());
                                    const auto &[pathStr, loaded] = *it;

                                    if (DECLARE_IMGUI_SCOPE(ItemFlag, ImGuiItemFlags_AutoClosePopups, false); ImGui::MenuItem(pathStr.c_str(), nullptr, loaded)) {
                                        std::filesystem::path path = utils::toPath(ranges::views::cast<char8_t>(pathStr));
                                        if (exists(path)) {
                                            loadAsset(std::move(path));
                                            ImGui::CloseCurrentPopup();
                                        }
                                        else {
                                            ImGui::OpenPopup(tempStringBuffer.write("File not exists##{}", i).view().c_str());
                                        }
                                    }

                                    bool erase = false;
                                    if (ImGui::BeginPopup(tempStringBuffer.write("File not exists##{}", i).view().c_str())) {
                                        imgui::widget::TextUnformatted("The file does not exist. Would you remove the file from the recents?");

                                        ImGui::Separator();

                                        if (ImGui::Button("Yes")) {
                                            erase = true;
                                            ImGui::CloseCurrentPopup();
                                        }
                                        ImGui::SetItemDefaultFocus();

                                        ImGui::SameLine();

                                        if (ImGui::Button("No")) {
                                            ImGui::CloseCurrentPopup();
                                        }

                                        ImGui::EndPopup();
                                    }
                                    else if (ImGui::BeginPopupContextItem()) {
                                        if (ImGui::MenuItem("Remove from Recents")) {
                                            erase = true;
                                        }

                                        ImGui::EndPopup();
                                    }

                                    if (erase) {
                                        it = static_cast<decltype(it)>(pImpl->guiState.recentAssets.erase(std::next(it, 1).base()));
                                    }
                                    else {
                                        ++it;
                                    }
                                }
                            }
                            else {
                                for (auto it = pImpl->guiState.recentAssetSearchResult.rbegin(); it != pImpl->guiState.recentAssetSearchResult.rend();) {
                                    const auto &[i, occurrenceOffsets] = *it;
                                    const auto &[pathStr, loaded] = pImpl->guiState.recentAssets[i];
                                
                                    const ImVec2 highlightPos = ImGui::GetCursorScreenPos();
                                    for (std::size_t offset : occurrenceOffsets) {
                                        const float textWidthFromBeginToStartOfOccurrence = ImGui::CalcTextSize(pathStr.data(), &pathStr[offset]).x;
                                        const ImVec2 textSizeFromBeginToEndOfOccurrence = ImGui::CalcTextSize(pathStr.data(), &pathStr[offset] + pImpl->guiState.recentAssetSearchText.size());
                                        ImGui::GetWindowDrawList()->AddRectFilled(
                                            highlightPos + ImVec2 { textWidthFromBeginToStartOfOccurrence, 0.f },
                                            highlightPos + textSizeFromBeginToEndOfOccurrence,
                                            0xFF00AABB);
                                    }
                                
                                    if (DECLARE_IMGUI_SCOPE(ItemFlag, ImGuiItemFlags_AutoClosePopups, false); ImGui::MenuItem(pathStr.c_str(), nullptr, loaded)) {
                                        std::filesystem::path path = utils::toPath(ranges::views::cast<char8_t>(pathStr));
                                        if (exists(path)) {
                                            loadAsset(std::move(path));
                                
                                            pImpl->guiState.recentAssetSearchText.clear();
                                            pImpl->guiState.recentAssetSearchResultInvalidated = true;
                                
                                            ImGui::CloseCurrentPopup();
                                        }
                                        else {
                                            ImGui::OpenPopup(tempStringBuffer.write("File not exists##{}", i).view().c_str());
                                        }
                                    }

                                    bool erase = false;
                                    if (ImGui::BeginPopup(tempStringBuffer.write("File not exists##{}", i).view().c_str())) {
                                        imgui::widget::TextUnformatted("The file does not exist. Would you remove the file from the recents?");

                                        ImGui::Separator();

                                        if (ImGui::Button("Yes")) {
                                            erase = true;
                                            ImGui::CloseCurrentPopup();
                                        }
                                        ImGui::SetItemDefaultFocus();

                                        ImGui::SameLine();
                                        if (ImGui::Button("No")) {
                                            ImGui::CloseCurrentPopup();
                                        }

                                        ImGui::EndPopup();
                                    }
                                    else if (ImGui::BeginPopupContextItem()) {
                                        if (ImGui::MenuItem("Remove from Recents")) {
                                            erase = true;
                                        }

                                        ImGui::EndPopup();
                                    }

                                    if (erase) {
                                        pImpl->guiState.recentAssets.erase(std::next(pImpl->guiState.recentAssets.begin(), it->first));
                                        pImpl->guiState.recentAssetSearchResultInvalidated = true;

                                        it = static_cast<decltype(it)>(pImpl->guiState.recentAssetSearchResult.erase(std::next(it, 1).base()));
                                    }
                                    else {
                                        ++it;
                                    }
                                }
                            }
                        }

                        ImGui::EndMenu();
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
                if (ImGui::Begin(tempStringBuffer.write("{}###{}", utils::toUTF8NFCString(viewport->assetWithDataBuffer.path.filename()), fmt::ptr(&viewport)).view().c_str(), &windowOpened, ImGuiWindowFlags_NoSavedSettings) && windowOpened) {
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

                    // Mark asset is unloaded in recentAssets
                    const auto recentAssetIt = std::ranges::find(pImpl->guiState.recentAssets, utils::toUTF8NFCString((*it)->assetWithDataBuffer.path), LIFT(get<0>));
                    if (recentAssetIt != pImpl->guiState.recentAssets.end()) {
                        recentAssetIt->second = false;
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