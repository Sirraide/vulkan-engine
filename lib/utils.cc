#include "utils.hh"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
