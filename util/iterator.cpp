#include "iterator.h"

#include <contrib/fmt/fmt/format.h>

#include <cassert>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <type_traits>
#include <vector>

static DIR* opendirat(int dir_fd, const char* name) noexcept {
    const int new_fd = ::openat(dir_fd, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_DIRECTORY | O_NOCTTY);

    if (new_fd < 0) {
        return nullptr;
    }

    if (DIR* dir = ::fdopendir(new_fd)) {
        return dir;
    } else {
        int last_errno = errno;
        ::close(new_fd);
        errno = last_errno;

        return nullptr;
    }
}

class DirectoryIterator::Impl {
public:
    explicit Impl(const std::filesystem::path& path);

    ~Impl();

    const DirectoryEntry* Next();

    bool Status();

    int Depth() const noexcept {
        return entries_.empty() ? 0 : entries_.size() - 1;
    }

    void DisableRecursionPending() noexcept {
        skip_ = true;
    }

    bool RecursionPending() const noexcept {
        return skip_;
    }

private:
    /// Path of the current entry relative to iterator's root.
    std::string path_;
    /// Stack of opened directories.
    std::vector<std::unique_ptr<DIR, decltype([](DIR* d) { ::closedir(d); })>> directories_;
    /// Sack of active entries.
    std::vector<std::pair<struct dirent*, DirectoryEntry>> entries_;

    struct stat stat_;

    bool skip_;
};

DirectoryIterator::Impl::Impl(const std::filesystem::path& path)
    : stat_()
    , skip_(false) {
    if (DIR* dir = ::opendir(path.c_str())) {
        path_.reserve(255);
        directories_.reserve(16);
        entries_.reserve(16);

        directories_.emplace_back(dir);
    } else {
        throw std::system_error(
            errno, std::system_category(), fmt::format("cannot open directory '{}'", path.c_str())
        );
    }
}

DirectoryIterator::Impl::~Impl() {
    entries_.clear();
    // Close directories from bottom to top.
    for (auto di = directories_.rbegin(), end = directories_.rend(); di != end; ++di) {
        di->reset();
    }
}

const DirectoryEntry* DirectoryIterator::Impl::Next() {
    if (directories_.empty()) {
        return nullptr;
    }
    // First call.
    if (entries_.empty()) {
        entries_.emplace_back();
        return &entries_.back().second;
    }

    if (entries_.back().second.is_directory_enter()) {
        if (skip_) {
            skip_ = false;

            // Append fake directory if not at the root entry.
            if (entries_.back().first) {
                directories_.emplace_back();
            }
            // Set exit event.
            entries_.back().second.exit_ = true;
            // Return exit event.
            return &entries_.back().second;
        }

        if (entries_.back().first) {
            if (DIR* dir = ::opendirat(::dirfd(directories_.back().get()), entries_.back().first->d_name)) {
                directories_.emplace_back(dir);
            } else {
                throw std::system_error(
                    errno, std::system_category(), fmt::format("cannot open directory")
                );
            }
        }
    } else {
        skip_ = false;
        // Pop visited directory.
        if (entries_.back().second.is_directory_exit()) {
            directories_.pop_back();

            if (directories_.empty()) {
                return nullptr;
            }
        }

        // Cut entry's name.
        path_.resize(path_.size() - entries_.back().second.filename().size());
        // Cut path separator.
        if (path_.size()) {
            assert(path_.back() == '/');

            path_.pop_back();
        }
        // Pop previous entry.
        entries_.pop_back();
    }

    while (dirent* ent = ::readdir(directories_.back().get())) {
        // Do not emit dot or dot-dot entries.
        if (ent->d_type == DT_DIR) {
            static_assert(std::extent<decltype(ent->d_name)>::value >= 3);

            if (ent->d_name[0] == '.') {
                if (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')) {
                    continue;
                }
            }
        }

        // Setup entry.
        entries_.emplace_back(ent, [this](const dirent* de) {
            DirectoryEntry entry;

            // Setup type.
            if (de->d_type == DT_DIR) {
                entry.type_ = std::filesystem::file_type::directory;
            } else if (de->d_type == DT_REG) {
                entry.type_ = std::filesystem::file_type::regular;
            } else if (de->d_type == DT_LNK) {
                entry.type_ = std::filesystem::file_type::symlink;
            } else {
                entry.type_ = std::filesystem::file_type::unknown;
            }

            // Setup name.
            entry.name_ = std::string_view(de->d_name);

            return entry;
        }(ent));

        // Setup path.
        if (path_.size()) {
            path_.append("/");
        }
        path_.append(entries_.back().second.filename());
        // Setup entry'spath.
        entries_.back().second.path_ = path_;

        // Return entry event.
        return &entries_.back().second;
    }

    // Set exit event.
    entries_.back().second.exit_ = true;
    // Return exit event.
    return &entries_.back().second;
}

bool DirectoryIterator::Impl::Status() {
    if (directories_.empty() || entries_.empty()) {
        return false;
    }
    // Already stated.
    if (entries_.back().second.stat_) {
        return true;
    }

    if (directories_.back()) {
        auto& entry = entries_.back().second;

        if (!::fstatat(
                ::dirfd(directories_.back().get()), entry.filename().data(), &stat_,
                AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW
            ))
        {
            entry.stat_ = &stat_;
        }
    }

    return false;
}

DirectoryIterator::DirectoryIterator(const std::filesystem::path& path)
    : impl_(new Impl(path)) {
}

DirectoryIterator::~DirectoryIterator() = default;

const DirectoryEntry* DirectoryIterator::Next() {
    return impl_->Next();
}

bool DirectoryIterator::Status() {
    return impl_->Status();
}

void DirectoryIterator::DisableRecursionPending() noexcept {
    impl_->DisableRecursionPending();
}

bool DirectoryIterator::RecursionPending() const noexcept {
    return impl_->RecursionPending();
}

int DirectoryIterator::Depth() const noexcept {
    return impl_->Depth();
}
