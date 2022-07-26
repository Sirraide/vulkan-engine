#include "utils.hh"

#include <chrono>
#include <cxxabi.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

std::string current_time() {
    /// Format the current time as hh:mm:ss.mmm using clock_gettime().
    timespec ts{};
    tm tm{};
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    return fmt::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
}

std::vector<char> map_file(std::string_view filename) {
    int fd = ::open(filename.data(), O_RDONLY);
    if (fd < 0) [[unlikely]]
        die("open(\"{}\") failed: {}", filename, ::strerror(errno));

    struct stat s {};
    if (::fstat(fd, &s)) [[unlikely]]
        die("fstat(\"{}\") failed: {}", filename, ::strerror(errno));
    auto sz = size_t(s.st_size);
    if (sz == 0) [[unlikely]]
        return {};

    auto* mem = (char*) ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (mem == MAP_FAILED) [[unlikely]]
        die("mmap(\"{}\", {}) failed: {}", filename, sz, ::strerror(errno));
    ::close(fd);

    std::vector<char> bytes(sz);
    std::memcpy(bytes.data(), mem, sz);
    if (::munmap(mem, sz)) [[unlikely]]
        die("munmap(\"{}\", {}) failed: {}", filename, sz, ::strerror(errno));
    return bytes;
}

std::string current_stacktrace() {
    void* buffer[15];
    auto nptrs = backtrace(buffer, 15);

    char** strings = backtrace_symbols(buffer, nptrs);
    if (strings == nullptr) return "";

    /// The standard mandates that the buffer used for __cxa_demangle()
    /// be allocated with malloc() since it may expand it using realloc().
    char* demangled_name = (char*) malloc(1024);
    defer { free(demangled_name); };

    /// Get the entries.
    std::string s;
    for (int i = 2; i < nptrs; i++) {
        /// The mangled name is between '(', and '+'.
        auto mangled_name = strings[i];
        auto left = std::strchr(mangled_name, '(');
        if (left == nullptr) {
            s += fmt::format("{}\n", strings[i]);
            continue;
        }

        left++;
        auto right = std::strchr(left, '+');
        if (right == nullptr || left == right) {
            s += fmt::format("{}\n", strings[i]);
            continue;
        }
        *right = '\0';

        /// Demangle the name.
        int status;
        size_t length = 1024;
        auto* ret = abi::__cxa_demangle(left, demangled_name, &length, &status);

        /// Append the demangled name if demangling succeeded.
        *right = '+';
        if (status == 0) {
            /// __cxa_demangle() may call realloc().
            demangled_name = ret;

            s.append(mangled_name, u64(left - mangled_name));
            s.append(demangled_name);
            s.append(right);
            s += '\n';
        } else s += fmt::format("{}\n", strings[i]);
    }

    free(strings);
    return s;
}
