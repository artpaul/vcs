#pragma once

#include <vcs/object/store.h>

#include <filesystem>

namespace Vcs {

class Repository {
public:
    Repository(const std::filesystem::path& path);

    /** Initalize a bare repository. */
    static void Initialize(const std::filesystem::path& path);

public:
    /**
     * @name Object storage
     * @{
     */

    Datastore Objects();

    const Datastore Objects() const;

    /**@}*/

protected:
    std::filesystem::path bare_path_;
    /// Object storage.
    Datastore odb_;
};

} // namespace Vcs
