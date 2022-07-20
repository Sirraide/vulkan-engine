#ifndef VULKAN_TEMPLATE_UTILS_HH
#define VULKAN_TEMPLATE_UTILS_HH

#include <cstdint>
#include <fmt/format.h>
#include <vulkan/vulkan_core.h>

#define CAT_(X, Y) X##Y
#define CAT(X, Y)  CAT_(X, Y)

#define STR_(X) #X
#define STR(X)  STR_(X)

#define CAR(X, ...) X
#define CDR(X, ...) __VA_ARGS__

#define nocopy(type)            \
    type(const type&) = delete; \
    type& operator=(const type&) = delete

#define nomove(type)       \
    type(type&&) = delete; \
    type& operator=(type&&) = delete

#ifdef __clang__
#    define nullable _Nullable
#    define nonnull  _Nonnull
#elifdef __GNUC__
#    define nullable
#    define nonnull __attribute__((nonnull))
#else
#    define nullable
#    define nonnull
#endif

#ifndef NDEBUG
#    define ENABLE_VALIDATION_LAYERS
#endif

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;

template <typename... args_t>
[[noreturn]] inline void die(fmt::format_string<args_t...> fmt_str, args_t&&... args) {
    fmt::print(stderr, fmt_str, std::forward<args_t>(args)...);
    fmt::print(stderr, "\n");
    std::exit(1);
}

template <typename... args_t>
inline void assert_ok(VkResult res, fmt::format_string<args_t...> fmt_str = "", args_t&&... args) {
    if (res != VK_SUCCESS) {
        fmt::print(stderr, "[Vulkan] ");
        die(fmt_str, std::forward<args_t>(args)...);
    }
}

#endif // VULKAN_TEMPLATE_UTILS_HH
