#include "change.h"

namespace Vcs {

Modifications CompareEntries(const PathEntry& x, const PathEntry& y) noexcept {
    Modifications flags;

    flags.content = x.size != y.size || x.id != y.id;

    if (IsFile(x.type) && IsFile(y.type)) {
        flags.attributes = (x.type == PathType::Executible) != (y.type == PathType::Executible);
        flags.type = (x.type == PathType::Symlink) != (y.type == PathType::Symlink);
    } else {
        flags.type = x.type != y.type;
    }

    return flags;
}

} // namespace Vcs
