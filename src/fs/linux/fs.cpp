#include "fs.h"
#include "db.h"

#include <cmd/local/bare.h>
#include <vcs/store/memory.h>

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

} // namespace

Filesystem::Filesystem(const MountOptions& options)
    : odb_(options.repository->Objects())
    , blobs_(odb_.Cache(Store::MemoryCache<>::Make()))
    , trees_(odb_.Cache(Store::MemoryCache<>::Make()))
    , stage_(trees_, options.tree)
    , tree_(options.tree) {
    root_time_ = timespec{.tv_sec = std::time(nullptr), .tv_nsec = 0};

    metabase_ = std::make_unique<Metabase>((options.state_path / "meta").string());

    euid_ = geteuid();
    egid_ = getegid();
}

Filesystem::~Filesystem() = default;

int Filesystem::GetAttr(const std::string_view path, struct stat* st, fuse_file_info*) {
    if (const auto e = stage_.GetEntry(path)) {
        SetupAttributes(*e, st);

        if (auto meta = metabase_->GetMetadata(path)) {
            st->st_ctim = meta->ctime;
            st->st_mtim = meta->mtime;
            st->st_atim = std::max(st->st_ctim, st->st_mtim);
        }
        return 0;
    }
    return -ENOENT;
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

    fi->fh = std::bit_cast<uint64_t>(new BlobHandle(blobs_.Load(e->id, e->data)));
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

    fi->fh = std::bit_cast<uint64_t>(new DirectoryHandle(trees_.LoadTree(e->id)));
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
