#include "actions.h"

#include <unordered_map>

namespace Vcs {

static const std::unordered_map<std::string_view, Action> actions = {
    // Actions.
    {"clean", Action::Clean},
    {"commit", Action::Commit},
    {"init", Action::Init},
    {"log", Action::Log},
    {"rm", Action::Remove},
    {"restore", Action::Restore},
    {"show", Action::Show},
    {"status", Action::Status},
    {"switch", Action::Switch},
    {"workspace", Action::Workspace},

    // Shortcuts.
    {"ci", Action::Commit},
    {"st", Action::Status},
    {"sw", Action::Switch},
    {"ws", Action::Workspace},

    // Tools.
    {"dump", Action::Dump},
    {"git", Action::Git},
};

Action ParseAction(const std::string_view name) noexcept {
    if (auto ai = actions.find(name); ai != actions.end()) {
        return ai->second;
    }
    return Action::Unknown;
}

} // namespace Vcs