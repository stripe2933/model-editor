#pragma once

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define MOVE_CAP(x) x = std::move(x)
#define INDEX_SEQ(Is, N, ...) [&]<auto ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#define LIFT(...) [](auto &&...args) noexcept(noexcept(__VA_ARGS__(FWD(args)...))) -> decltype(auto) { return __VA_ARGS__(FWD(args)...); }

#define CONCAT_INNER(a, b) a##b
#define CONCAT(a, b) CONCAT_INNER(a, b)

#if defined(__clang__)
#define LIFETIMEBOUND [[clang::lifetimebound]]
#elif defined(_MSC_VER)
#define LIFETIMEBOUND [[msvc::lifetimebound]]
#else
#define LIFETIMEBOUND
#endif

#if !__has_include(<tchar.h>)
#define TEXT(StringLiteral) "" StringLiteral
#endif

#if defined(__APPLE__) && !defined(APPLE_USE_VULKAN)
#define DECLARE_AUTORELEASEPOOL auto CONCAT(_autoreleasepool_, __COUNTER__) = TransferPtr(NS::AutoreleasePool::alloc()->init())
#endif