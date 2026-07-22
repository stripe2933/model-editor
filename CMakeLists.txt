cmake_minimum_required(VERSION 3.24)

option(APPLE_USE_VULKAN "Use Vulkan backend in Apple platform")

if (APPLE_USE_VULKAN)
    list(APPEND VCPKG_MANIFEST_FEATURES "apple-use-vulkan")
endif()

project(model-editor
    VERSION 0.1.0
    LANGUAGES CXX
)

option(USE_TRACY "Use Tracy for frame profiling")

find_package(boost_container CONFIG REQUIRED)
find_package(boost_container_hash CONFIG REQUIRED)
find_package(boost_iostreams CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)

add_executable(model-editor WIN32 MACOSX_BUNDLE
    App.cpp
    App.hpp
    main.cpp
    impl.cpp
    utils/macros.hpp
    utils/ranges.hpp
)
target_compile_features(model-editor PRIVATE cxx_std_23)
target_include_directories(model-editor PRIVATE
    thirdparty/include
)
target_precompile_headers(model-editor PRIVATE pch.hpp)
target_link_libraries(model-editor PRIVATE
    Boost::container
    Boost::container_hash
    Boost::iostreams
    fastgltf::fastgltf
    fmt::fmt
    glfw
    glm::glm
    imgui::imgui
    meshoptimizer::meshoptimizer
    Tracy::TracyClient
)

set_source_files_properties(impl.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if (USE_TRACY)
    target_compile_definitions(model-editor PRIVATE TRACY_ENABLE)
endif()

if (APPLE)
    set_target_properties(model-editor PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Model Editor"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 LEE KYOUNGHEON"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.leekyoungheon.modeleditor"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.leekyoungheon.modeleditor"
    )
endif()

if (APPLE AND NOT APPLE_USE_VULKAN)
    enable_language(OBJCXX)

    target_sources(model-editor PRIVATE
        metal/Frame.cpp
        metal/Frame.hpp
        metal/Gpu.hpp
        metal/glfw_metal.hpp
        metal/glfw_metal.mm
        metal/imgui_metal_cpp.mm
    )
    target_link_libraries(model-editor PRIVATE
        "-framework Foundation"
        "-framework GameController"
        "-framework Metal"
        "-framework QuartzCore"
    )

    set_target_properties(model-editor PROPERTIES
        XCODE_ATTRIBUTE_MTL_ENABLE_DEBUG_INFO[variant=Debug] "INCLUDE_SOURCE"
        XCODE_ATTRIBUTE_MTL_ENABLE_DEBUG_INFO[variant=RelWithDebInfo] "INCLUDE_SOURCE"
    )
    set_source_files_properties(metal/glfw_metal.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
else()
    target_sources(model-editor PRIVATE
        utils/vku.hpp
        vulkan/Frame.cpp
        vulkan/Frame.hpp
        vulkan/Gpu.hpp
        vulkan/Swapchain.hpp
    )
    target_link_libraries(model-editor PRIVATE
        Vulkan::Vulkan
    )

    if (APPLE AND APPLE_USE_VULKAN)
        target_compile_definitions(model-editor PRIVATE APPLE_USE_VULKAN)
    endif()
endif()