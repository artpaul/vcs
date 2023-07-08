#pragma once

#include <vcs/object/hashid.h>

#include <filesystem>
#include <string>

namespace Vcs {

class Repository;

} // namespace Vcs

namespace Vcs::Fs {

struct MountOptions {
    Repository* repository = nullptr;
    /// Root of the working tree.
    HashId tree = HashId();

    /// Path to store all data related to workspace.
    std::filesystem::path state_path;

    /// Name of the running program.
    std::string name = "vcs-fs";
    /// Mounting root.
    std::filesystem::path mount_path;
    /// Enable debug mode.
    bool debug = false;
    /// Run in foreground mode.
    bool foreground = false;
    /// Run in multithreaded mode.
    bool multithreaded = true;
};

int MountWorktree(const MountOptions& options);

} // namespace Vcs::Fs
