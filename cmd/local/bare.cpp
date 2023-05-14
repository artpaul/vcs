#include "bare.h"

#include <vcs/store/loose.h>

namespace Vcs {

Repository::Repository(const std::filesystem::path& path)
    : bare_path_(path) {
    odb_ = Datastore::Make<Store::Loose>(path / "objects");
}

void Repository::Initialize(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);

    std::filesystem::create_directory(path / "config");
    std::filesystem::create_directory(path / "objects");
    std::filesystem::create_directory(path / "workspaces");
}

Datastore Repository::Objects() {
    return odb_;
}

const Datastore Repository::Objects() const {
    return odb_;
}

} // namespace Vcs
