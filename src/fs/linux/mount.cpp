#include "mount.h"
#include "fs.h"

#include <contrib/fmt/fmt/format.h>
#include <contrib/libfuse/include/fuse.h>

#include <cassert>
#include <functional>
#include <sys/stat.h>
#include <sys/types.h>

namespace Vcs::Fs {

static std::unique_ptr<Filesystem> gFs;

template <typename F, typename... Args>
static auto Invoke(F&& func, Args&&... args) -> std::invoke_result_t<F, Filesystem*, Args...> {
    assert(gFs);

    try {
        return std::invoke(std::forward<F>(func), gFs.get(), std::forward<Args>(args)...);
    } catch (...) {
        using R = std::invoke_result_t<F, Filesystem*, Args...>;

        if constexpr (std::is_same_v<R, int>) {
            return -EIO;
        }
        if constexpr (std::is_pointer_v<R>) {
            return nullptr;
        }
    }
}

static void* FsInit(struct fuse_conn_info*, struct fuse_config* cfg) {
    cfg->attr_timeout = 1.0;
    cfg->auto_cache = 1;
    cfg->entry_timeout = 1.0;
    cfg->negative_timeout = 1.0;
    cfg->nullpath_ok = 0;

    return gFs.get();
}

static void FsDestory(void*) {
    gFs.reset();
}

static int FsChmod(const char* path, mode_t mode, struct fuse_file_info* fi) {
    return Invoke(&Filesystem::Chmod, path ? std::string_view(path + 1) : std::string_view(), mode, fi);
}

static int FsGetAttr(const char* path, struct stat* st, struct fuse_file_info* fi) {
    return Invoke(&Filesystem::GetAttr, path ? std::string_view(path + 1) : std::string_view(), st, fi);
}

static int FsOpen(const char* path, struct fuse_file_info* fi) {
    return Invoke(&Filesystem::Open, std::string_view(path + 1), fi);
}

static int FsRead(const char*, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    return Invoke(&Filesystem::Read, buf, size, offset, fi);
}

static int FsOpenDir(const char* path, struct fuse_file_info* fi) {
    return Invoke(&Filesystem::OpenDir, std::string_view(path + 1), fi);
}

static int FsReadDir(
    const char*,
    void* buf,
    fuse_fill_dir_t filler,
    off_t off,
    struct fuse_file_info* fi,
    enum fuse_readdir_flags flags
) {
    return Invoke(&Filesystem::ReadDir, buf, filler, off, fi, flags);
}

static int FsReadLink(const char* path, char* buf, size_t size) {
    return Invoke(&Filesystem::ReadLink, std::string_view(path + 1), buf, size);
}

static int FsRelease(const char*, struct fuse_file_info* fi) {
    return Invoke(&Filesystem::Release, fi);
}

static int FsReleaseDir(const char*, struct fuse_file_info* fi) {
    return Invoke(&Filesystem::ReleaseDir, fi);
}

static int FsStatFs(const char*, struct statvfs* fs) {
    return Invoke(&Filesystem::StatFs, fs);
}

int MountWorktree(const MountOptions& options) {
    assert(options.mount_path.is_absolute());
    assert(options.repository);

    fuse_operations ops{};
    int ret = 0;

    ops.init = FsInit;
    ops.destroy = FsDestory;
    ops.chmod = FsChmod;
    ops.getattr = FsGetAttr;
    ops.open = FsOpen;
    ops.opendir = FsOpenDir;
    ops.read = FsRead;
    ops.readdir = FsReadDir;
    ops.readlink = FsReadLink;
    ops.release = FsRelease;
    ops.releasedir = FsReleaseDir;
    ops.statfs = FsStatFs;

    // Don't mask creation mode, kernel already did that.
    ::umask(0);

    fuse_args args = FUSE_ARGS_INIT(0, nullptr);

    if (fuse_opt_add_arg(&args, options.name.c_str())
        || fuse_opt_add_arg(&args, "-odefault_permissions,fsname=vcs-rw")
        || fuse_opt_add_arg(&args, "-oauto_unmount,allow_other,allow_root")
        || (options.debug && fuse_opt_add_arg(&args, "-odebug")))
    {
        fuse_opt_free_args(&args);
        return 1;
    }

    gFs.reset(new Filesystem(options));

    std::unique_ptr<fuse, decltype([](fuse* p) { fuse_destroy(p); })> fs;

    // Create filesystem object.
    fs.reset(fuse_new(&args, &ops, sizeof(ops), gFs.get()));

    // Mount filesystem.
    if (fuse_mount(fs.get(), options.mount_path.c_str()) == 0) {
        fuse_daemonize(1);

        if (options.multithreaded) {
            std::unique_ptr<fuse_loop_config, decltype([](fuse_loop_config* p) {
                                fuse_loop_cfg_destroy(p);
                            })>
                config(fuse_loop_cfg_create());

            ret = fuse_loop_mt(fs.get(), config.get());
        } else {
            ret = fuse_loop(fs.get());
        }

        fuse_unmount(fs.get());
    }

    fs.reset();
    gFs.reset();

    // fuse_main

    return ret;
}

} // namespace Vcs::Fs
