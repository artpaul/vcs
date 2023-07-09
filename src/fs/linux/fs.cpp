#include "fs.h"
#include "db.h"

#include <cmd/local/bare.h>
#include <vcs/store/memory.h>

#include <util/split.h>

#include <contrib/fmt/fmt/format.h>
#include <contrib/libfuse/include/fuse.h>

#include <cassert>
#include <sys/stat.h>
#include <sys/types.h>

static constexpr bool operator<(const timespec& a, const timespec& b) noexcept {
    return std::make_tuple(a.tv_sec, a.tv_nsec) < std::make_tuple(b.tv_sec, b.tv_nsec);
}

namespace Vcs::Fs {
namespace {

constexpr blkcnt_t CalculateBlockCount(const size_t size) noexcept {
    return ((size + (4096 - 1)) & ~(4096 - 1)) / 4096;
}

constexpr Meta MakeMeta(const PathEntry& e) noexcept {
    Meta meta;

    // Common attributes.
    meta.id = e.id;
    meta.size = e.size;
    // Type of the entry.
    switch (e.type) {
        case PathType::Unknown:
            break;
        case PathType::File:
            meta.mode = S_IFREG | 0644;
            break;
        case PathType::Directory:
            meta.mode = S_IFDIR | 0755;
            meta.size = 4096;
            break;
        case PathType::Executible:
            meta.mode = S_IFREG | 0644 | S_IXUSR | S_IXGRP | S_IXOTH;
            break;
        case PathType::Symlink:
            meta.mode = S_IFLNK | 0755;
            break;
    }
    // Timestamps.
    meta.ctime = timespec();
    meta.mtime = timespec();

    return meta;
}

} // namespace

auto Filesystem::Directory::MakeEmpty() -> std::shared_ptr<Directory> {
    return std::make_shared<Directory>();
}

auto Filesystem::Directory::MakeFromTree(const Tree& tree) -> std::shared_ptr<Directory> {
    auto dir = std::make_shared<Directory>();

    for (const auto& e : tree.Entries()) {
        auto ei = dir->entries.emplace_hint(dir->entries.end(), e.Name(), Directory::Entry());

        ei->second.meta = MakeMeta(static_cast<PathEntry>(e));
    }

    return dir;
}

Filesystem::Filesystem(const MountOptions& options)
    : odb_(options.repository->Objects())
    , blobs_(odb_.Cache(Store::MemoryCache<>::Make()))
    , trees_(odb_.Cache(Store::MemoryCache<>::Make()))
    , stage_(trees_, options.tree)
    , tree_(options.tree) {
    root_time_ = timespec{.tv_sec = std::time(nullptr), .tv_nsec = 0};

    metabase_ = std::make_unique<Metabase>((options.state_path / "meta").string());

    root_ = tree_ ? Directory::MakeFromTree(trees_.LoadTree(tree_)) : Directory::MakeEmpty();

    euid_ = geteuid();
    egid_ = getegid();
}

Filesystem::~Filesystem() = default;

int Filesystem::Chmod(const std::string_view path, mode_t mode, fuse_file_info* fi) {
    if (fi) {
        if (fi->fh) {
            ;
        }
    }

    if (const auto e = stage_.GetEntry(path)) {
        Meta meta = MakeMeta(*e);

        if (auto data = metabase_->GetMetadata(path)) {
            // Tombstone.
            if (data->index() == 0) {
                meta.ctime = root_time_;
                meta.mtime = root_time_;
            }
            // Timestamps.
            if (data->index() == 1) {
                const Timestamps& ts = std::get<1>(*data);

                meta.ctime = ts.ctime;
                meta.mtime = ts.mtime;
            }
            // Full metadata.
            if (data->index() == 2) {
                meta = std::get<2>(*data);
            }
        } else {
            meta.ctime = root_time_;
            meta.mtime = root_time_;
        }

        // Update permissions.
        meta.mode = (meta.mode & ~ALLPERMS) | (mode & ALLPERMS);

        // Save metadata.
        metabase_->PutMeta(path, meta);

        return 0;
    }

    return -ENOENT;
}

int Filesystem::GetAttr(const std::string_view path, struct stat* st, fuse_file_info* fi) {
    auto setup_from_meta = [this](const Meta& meta, struct stat* st) {
        std::memset(st, 0, sizeof(struct stat));
        // Common attributes.
        st->st_mode = meta.mode;
        st->st_nlink = 1;
        st->st_size = meta.size;
        // Directory type.
        if (S_ISDIR(meta.mode)) {
            st->st_nlink = 2;
            st->st_size = 4096;
        }
        // Clear internal flag.
        st->st_mode = st->st_mode & ~S_MUTABLE;
        // Timestamps.
        st->st_ctim = meta.ctime;
        st->st_mtim = meta.mtime;
        st->st_atim = std::max(st->st_ctim, st->st_mtim);
        // System-wide attributes.
        st->st_gid = egid_;
        st->st_uid = euid_;
        st->st_blksize = 4096;
        st->st_blocks = CalculateBlockCount(st->st_size);
    };

    if (fi) {
        if (fi->fh) {
            setup_from_meta(std::bit_cast<const BaseHandle*>(fi->fh)->meta, st);
            return 0;
        }
    }

    if (auto data = metabase_->GetMetadata(path)) {
        // Tombstone.
        if (data->index() == 0) {
            return -ENOENT;
        }
        // Timestamps.
        if (data->index() == 1) {
            const Timestamps& ts = std::get<1>(*data);

            if (const auto e = stage_.GetEntry(path)) {
                SetupAttributes(*e, st);
                // Timestamps.
                st->st_ctim = ts.ctime;
                st->st_mtim = ts.mtime;
                st->st_atim = std::max(st->st_ctim, st->st_mtim);
                return 0;
            } else {
                return -ENOENT;
            }
        }
        // Full metadata.
        if (data->index() == 2) {
            setup_from_meta(std::get<2>(*data), st);
            return 0;
        }

        return -ENOENT;
    }

    if (const auto e = stage_.GetEntry(path)) {
        SetupAttributes(*e, st);
        return 0;
    }

    return -ENOENT;
}

int Filesystem::Mkdir(const std::string_view path, mode_t mode) {
    const auto& parts = SplitPath(path);
    std::shared_lock lock(root_mutex_);

    if (auto parent = GetMutableParentNoLock(parts)) {
        std::lock_guard lock(parent->mutex);

        auto ei = parent->entries.find(parts.back());
        // Check for existence and insert an entry.
        if (ei == parent->entries.end()) {
            ei = parent->entries.emplace(parts.back(), Directory::Entry()).first;
        } else if (ei->second.state != Directory::State::Deleted) {
            return -EEXIST;
        }
        // Setup the entry.
        ei->second.meta.id = HashId();
        ei->second.meta.mode = S_MUTABLE | S_IFDIR | (mode & ~ALLPERMS);
        ei->second.meta.size = 0;
        ei->second.meta.ctime = timespec{.tv_sec = time(0), .tv_nsec = 0};
        ei->second.meta.mtime = ei->second.meta.ctime;
        ei->second.directory = nullptr;

        // Save metadata.
        metabase_->PutMeta(path, ei->second.meta);
        // TODO: save parent.
        return 0;
    } else {
        return -ENOTDIR;
    }
}

int Filesystem::Open(const std::string_view path, fuse_file_info* fi) {
    const auto e = stage_.GetEntry(path);
    // Read-only right now.
    if (fi->flags & (O_APPEND | O_TRUNC | O_WRONLY)) {
        return -EROFS;
    }
    // Check entry exists.
    if (!e) {
        return -ENOENT;
    }
    if (IsDirectory(e->type)) {
        return -EISDIR;
    }

    fi->fh = std::bit_cast<uint64_t>(new BlobHandle(MakeMeta(*e), blobs_.Load(e->id, e->data)));
    fi->keep_cache = 1;
    fi->noflush = 1;

    // Remember access time.
    metabase_->PutTimestamps(path, Timestamps{.ctime = root_time_, .mtime = root_time_});

    return 0;
}

int Filesystem::OpenDir(const std::string_view path, fuse_file_info* fi) {
    const auto e = stage_.GetEntry(path);
    // Check entry exists.
    if (!e) {
        return -ENOENT;
    }
    if (!IsDirectory(e->type)) {
        return -ENOTDIR;
    }

    fi->fh = std::bit_cast<uint64_t>(new DirectoryHandle(MakeMeta(*e), trees_.LoadTree(e->id)));
    fi->cache_readdir = 1;
    fi->keep_cache = 1;
    fi->noflush = 1;

    return 0;
}

int Filesystem::Read(char* buf, size_t size, off_t offset, fuse_file_info* fi) {
    if (!fi) {
        return -EBADF;
    }

    auto handle = std::bit_cast<const BlobHandle*>(fi->fh);
    auto obj = handle->blob;

    if (obj->Type() == DataType::Blob) {
        if (offset >= obj->Size()) {
            return 0;
        }
        // Adjust reading size.
        size = std::min(size, obj->Size() - offset);
        // Copy data.
        std::memcpy(buf, static_cast<const char*>(obj->Data()) + offset, size);
        // Return number of readed bytes.
        return size;
    }

    return -EIO;
}

int Filesystem::ReadDir(void* buf, fuse_fill_dir_t filler, off_t, fuse_file_info* fi, fuse_readdir_flags) {
    filler(buf, ".", nullptr, 0, fuse_fill_dir_flags(0));
    filler(buf, "..", nullptr, 0, fuse_fill_dir_flags(0));

    auto handle = std::bit_cast<const DirectoryHandle*>(fi->fh);

    for (const auto& e : handle->tree.Entries()) {
        struct stat st;
        // Setup attributes.
        SetupAttributes(static_cast<PathEntry>(e), &st);
        // Fill the buffer.
        filler(buf, std::string(e.Name()).c_str(), &st, 0, FUSE_FILL_DIR_PLUS);
    }

    return 0;
}

int Filesystem::ReadLink(const std::string_view path, char* buf, size_t size) {
    const auto e = stage_.GetEntry(path);
    // Check entry exists.
    if (!e) {
        return -ENOENT;
    }
    if (!IsSymlink(e->type) || size == 0) {
        return -EINVAL;
    }

    const auto blob = blobs_.LoadBlob(e->id);
    // Adjust reading size.
    size = std::min(size - 1, blob.Size());
    // Copy data.
    std::memcpy(buf, blob.Data(), size);
    // Set null terminator.
    buf[size] = '\0';

    // Remember access time.
    metabase_->PutTimestamps(path, Timestamps{.ctime = root_time_, .mtime = root_time_});

    return 0;
}

int Filesystem::Release(struct fuse_file_info* fi) {
    if (fi) {
        delete std::bit_cast<BlobHandle*>(fi->fh);
    }
    return 0;
}

int Filesystem::ReleaseDir(struct fuse_file_info* fi) {
    if (fi) {
        delete std::bit_cast<DirectoryHandle*>(fi->fh);
    }
    return 0;
}

int Filesystem::StatFs(struct statvfs* fs) {
    fs->f_flag = ST_NOATIME;
    fs->f_bsize = 4096;
    fs->f_namemax = 255;
    return 0;
}

auto Filesystem::GetMutableParentNoLock(const std::vector<std::string_view>& parts) -> DirectoryPtr {
    DirectoryPtr parent = root_;

    if (parts.empty() || parts.size() == 1) {
        return parent;
    }

    for (size_t i = 0, end = parts.size() - 1; i != end; ++i) {
        std::unique_lock lock(parent->mutex);

        if (auto ei = parent->entries.find(parts[i]); ei != parent->entries.end()) {
            auto& entry = ei->second;
            // The node is not a directory or was deleted.
            if (!S_ISDIR(entry.meta.mode) || entry.state == Directory::State::Deleted) {
                return nullptr;
            }
            if (!entry.directory) {
                if (entry.meta.id) {
                    entry.directory = Directory::MakeFromTree(trees_.LoadTree(entry.meta.id));
                } else {
                    entry.directory = Directory::MakeEmpty();
                }
            }
            if (auto d = entry.directory) {
                lock.unlock();
                parent = std::move(d);
            }
        } else {
            return {};
        }
    }

    return parent;
}

void Filesystem::SetupAttributes(const PathEntry& e, struct stat* st) const noexcept {
    std::memset(st, 0, sizeof(struct stat));
    // Common attributes.
    st->st_nlink = 1;
    st->st_size = e.size;
    // Type of the entry.
    switch (e.type) {
        case PathType::Unknown:
            break;
        case PathType::File:
            st->st_mode = S_IFREG | 0644;
            break;
        case PathType::Directory:
            st->st_mode = S_IFDIR | 0755;
            st->st_nlink = 2;
            st->st_size = 4096;
            break;
        case PathType::Executible:
            st->st_mode = S_IFREG | 0644 | S_IXUSR | S_IXGRP | S_IXOTH;
            break;
        case PathType::Symlink:
            st->st_mode = S_IFLNK | 0755;
            break;
    }
    // Clear internal flag.
    st->st_mode = st->st_mode & ~S_MUTABLE;
    // Timestamps.
    st->st_ctim = root_time_;
    st->st_mtim = root_time_;
    st->st_atim = std::max(st->st_ctim, st->st_mtim);
    // System-wide attributes.
    st->st_gid = egid_;
    st->st_uid = euid_;
    st->st_blksize = 4096;
    st->st_blocks = CalculateBlockCount(st->st_size);
}

} // namespace Vcs::Fs
