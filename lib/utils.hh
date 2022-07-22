#ifndef VULKAN_TEMPLATE_UTILS_HH
#define VULKAN_TEMPLATE_UTILS_HH
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <cstdint>
#include <fmt/format.h>
#include <vulkan/vulkan_core.h>
#include <vector>

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

#define defer auto CAT($$defer_struct_instance_, __COUNTER__) = defer_type_operator_lhs::instance % [&]

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
    fmt::print(stderr, "\033[31m[Fatal] ");
    fmt::print(stderr, fmt_str, std::forward<args_t>(args)...);
    fmt::print(stderr, "\n");
    std::exit(1);
}

template <typename... args_t>
inline void assert_success(VkResult res, fmt::format_string<args_t...> fmt_str = "", args_t&&... args) {
    if (res != VK_SUCCESS) {
        fmt::print(stderr, "[Vulkan] ");
        fmt::print(stderr, fmt_str, std::forward<args_t>(args)...);
        fmt::print(stderr, "\n");
        std::exit(1);
    }
}

template <typename callable_t>
struct defer_type {
    using callable_type = callable_t;
    const callable_type function;
    explicit defer_type(callable_t _function) : function(_function) {}
    inline ~defer_type() { function(); }
    defer_type(const defer_type&)            = delete;
    defer_type(defer_type&&)                 = delete;
    defer_type& operator=(const defer_type&) = delete;
    defer_type& operator=(defer_type&&)      = delete;
};

struct defer_type_operator_lhs {
    static defer_type_operator_lhs instance;
    template <typename callable_t>
    auto operator%(callable_t rhs) -> defer_type<callable_t> { return defer_type<callable_t>(rhs); }
};

std::vector<char> map_file(std::string_view filename);

#endif // VULKAN_TEMPLATE_UTILS_HH
