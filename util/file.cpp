#include "file.h"

#include <contrib/fmt/fmt/format.h>
#include <contrib/fmt/fmt/std.h>

#include <system_error>

#if defined(_unix_)
#   include <errno.h>
#   include <fcntl.h>
#   include <sys/stat.h>
#   include <sys/types.h>
#   include <unistd.h>

#   define INVALID_HANDLE_VALUE (-1)
#endif

static constexpr std::filesystem::perms DefaultPremissions =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
    | std::filesystem::perms::group_read | std::filesystem::perms::others_read;

File::File()
    : fd_(INVALID_HANDLE_VALUE) {
}

File::File(FHANDLE fd)
    : fd_(fd) {
}

File::File(File&& other)
    : fd_(other.fd_) {
    other.fd_ = INVALID_HANDLE_VALUE;
}

File::~File() {
    Close();
}

File File::ForAppend(const std::filesystem::path& path, bool create) {
    int fd = ::open(path.c_str(), O_APPEND | O_RDWR | (create ? O_CREAT : 0), DefaultPremissions);
    if (fd == -1) {
        throw std::system_error(
            errno, std::system_category(), fmt::format("cannot open file for appending '{}'", path)
        );
    }
    return File(fd);
}

File File::ForRead(const std::filesystem::path& path, bool follow) {
    int fd = ::open(path.c_str(), O_RDONLY | (follow ? 0 : O_NOFOLLOW));
    if (fd == -1) {
        throw std::system_error(
            errno, std::system_category(), fmt::format("cannot open file for reading '{}'", path)
        );
    }
    return File(fd);
}

File File::ForOverwrite(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, DefaultPremissions);
    if (fd == -1) {
        throw std::system_error(
            errno, std::system_category(), fmt::format("cannot open file for writing '{}'", path)
        );
    }
    return File(fd);
}

bool File::Valid() const {
    return fd_ != INVALID_HANDLE_VALUE;
}

int File::Close() {
    if (fd_ != INVALID_HANDLE_VALUE) {
        return ::close(fd_);
    }
    return EBADF;
}

void File::FlushData() {
    if (ssize_t ret = ::fdatasync(fd_); ret < 0) {
        throw std::system_error(errno, std::system_category(), "cannot flush data");
    }
}

size_t File::Load(void* const p, size_t len) {
    std::byte* buf = reinterpret_cast<std::byte*>(p);

    while (len > 0) {
        if (const size_t read = this->Read(p, len)) {
            len -= read;
            buf += read;
        } else {
            break;
        }
    }

    return buf - reinterpret_cast<std::byte*>(p);
}

size_t File::Read(void* buf, size_t len) {
    if (ssize_t ret = ::read(fd_, buf, len); ret < 0) {
        throw std::system_error(errno, std::system_category(), "cannot read data from file");
    } else {
        return ret;
    }
}

size_t File::Read(void* buf, size_t len, off_t offset) {
    if (ssize_t ret = ::pread(fd_, buf, len, offset); ret < 0) {
        throw std::system_error(errno, std::system_category(), "cannot read data from file");
    } else {
        return ret;
    }
}

size_t File::Write(const void* buf, size_t len) {
    if (ssize_t ret = ::write(fd_, buf, len); ret < 0) {
        throw std::system_error(errno, std::system_category(), "cannot write data to file");
    } else {
        return ret;
    }
}

size_t File::Size() const {
    struct stat buf { };
    if (int ret = ::fstat(fd_, &buf); ret < 0) {
        throw std::system_error(errno, std::system_category(), "cannot get file stat");
    } else {
        return buf.st_size;
    }
}

void StringToFile(const std::filesystem::path& path, const std::string_view value) {
    auto file = File::ForOverwrite(path);

    for (const char *p = value.data(), *end = value.data() + value.size(); p != end;) {
        p += file.Write(p, end - p);
    }
}

std::string StringFromFile(const std::filesystem::path& path) {
    auto file = File::ForRead(path);
    std::string result;

    result.reserve(file.Size());

    char buf[4096];

    while (size_t read = file.Read(buf, sizeof(buf))) {
        result.append(buf, read);
    }

    return result;
}
