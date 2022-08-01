#include "utils.hh"

#include <chrono>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool assertion_error::use_colour = true;

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
