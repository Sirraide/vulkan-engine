#ifndef HPUTILS_UTILS_BASE_HH
#define HPUTILS_UTILS_BASE_HH

#include <cstdint>
#include <fmt/format.h>
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

#define ASSERT(condition, ...)                                   \
    do {                                                         \
        if (!(condition))                                        \
            throw assertion_error(STR(condition),                \
                FILENAME,                                        \
                __LINE__,                                        \
                __PRETTY_FUNCTION__ __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define FILENAME (this_file_name())

#define UNREACHABLE() ASSERT(false, "UNREACHABLE")

/// Check if an an expression is a valid enum value.
#define ENUMERATOR(x, enumeration) __extension__({                                                      \
    using temp_value_type = std::remove_cvref_t<decltype(x)>;                                              \
    temp_value_type temp_value = x;                                                                        \
    temp_value > temp_value_type(enumeration::$$min) && temp_value <= temp_value_type(enumeration::$$max); \
})

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

/// Get the current time as hh:mm:ss.mmm.
std::string current_time();

template <typename... args_t>
inline void info(fmt::format_string<args_t...> fmt_str, args_t&&... args) {
    fmt::print(stderr, "\033[33m[{}] Info: ", current_time());
    fmt::print(stderr, fmt_str, std::forward<args_t>(args)...);
    fmt::print(stderr, "\033[m\n");
}

template <typename... args_t>
inline void err(fmt::format_string<args_t...> fmt_str, args_t&&... args) {
    fmt::print(stderr, "\033[31m[{}] Error: ", current_time());
    fmt::print(stderr, fmt_str, std::forward<args_t>(args)...);
    fmt::print(stderr, "\033[m\n");
}

template <typename... args_t>
[[noreturn]] inline void die(fmt::format_string<args_t...> fmt_str, args_t&&... args) {
    fmt::print(stderr, "\033[1;31m[{}] Fatal: ", current_time());
    fmt::print(stderr, fmt_str, std::forward<args_t>(args)...);
    fmt::print(stderr, "\033[m\n");
    std::exit(1);
}

template <typename callable_t>
struct defer_type {
    using callable_type = callable_t;
    const callable_type function;
    explicit defer_type(callable_t _function) : function(_function) {}
    inline ~defer_type() { function(); }
    defer_type(const defer_type&) = delete;
    defer_type(defer_type&&) = delete;
    defer_type& operator=(const defer_type&) = delete;
    defer_type& operator=(defer_type&&) = delete;
};

struct defer_type_operator_lhs {
    static defer_type_operator_lhs instance;
    template <typename callable_t>
    auto operator%(callable_t rhs) -> defer_type<callable_t> { return defer_type<callable_t>(rhs); }
};

struct assertion_error : public std::runtime_error {
    static bool use_colour;

    explicit assertion_error(std::string&& message) : std::runtime_error(std::forward<std::string>(message)) {}
    template <typename... args_t>

    explicit assertion_error(const std::string& cond_mess, const char* file, int line, const char* pretty_function,
        fmt::format_string<args_t...> fmt_str = "", args_t&&... args)
        : std::runtime_error([&] {
              std::string m;

              /// The extra \033[m may seem superfluous, but having them as extra delimiters
              /// makes translating the colour codes into html tags easier.
              if (use_colour) {
                  m = fmt::format("\033[1;31mAssertion Error\033[m\033[33m\n"
                                  "    In internal file\033[m \033[32m{}:{}\033[m\033[33m\n"
                                  "    In function\033[m \033[32m{}\033[m\033[33m\n"
                                  "    Assertion failed:\033[m \033[34m{}",
                      file, line, pretty_function, cond_mess);
                  auto str = fmt::format(fmt_str, std::forward<args_t>(args)...);
                  if (!str.empty()) {
                      m += fmt::format("\033[m\n\033[33m    Message:\033[m \033[31m");
                      m += str;
                  }
                  m += "\033[m\n";
              } else {
                  m = fmt::format("Assertion Error\n"
                                  "    In internal file {}:{}\n"
                                  "    In function {}\n"
                                  "    Assertion failed: {}\n",
                      file, line, pretty_function, cond_mess);
                  auto str = fmt::format(fmt_str, std::forward<args_t>(args)...);
                  if (!str.empty()) {
                      m += fmt::format("    Message: ");
                      m += str;
                      m += "\n";
                  }
              }
              return m;
          }()) {}

    template <typename... args_t>
    explicit assertion_error(const assertion_error& other, args_t&&... args)
        : std::runtime_error([&] {
              std::string m{ other.what() };
              if (!m.ends_with("\n")) m += '\n';
              ((m += args), ...);
              return m;
          }()) {}
};

consteval const char* this_file_name(const char* fname = __builtin_FILE()) {
    const char *ptr = __builtin_strchr(fname, '/'), *last = ptr;
    if (!last) return fname;
    while (last) {
        ptr = last;
        last = __builtin_strchr(last + 1, '/');
    }
    return ptr + 1;
}

std::vector<char> map_file(std::string_view filename);

#endif // HPUTILS_UTILS_BASE_HH
