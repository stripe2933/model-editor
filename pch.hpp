#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <numeric>
#include <ranges>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/container_hash/hash.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_XYZW_ONLY
#include <glm/packing.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <tracy/Tracy.hpp>

#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#else
#define VK_ENABLE_BETA_EXTENSIONS
#define VULKAN_HPP_NO_DEFAULT_DISPATCHER
#define VULKAN_HPP_NO_SMART_HANDLE
#define VULKAN_HPP_NO_TO_STRING
#include <vulkan/vulkan_raii.hpp>
#endif