#pragma once

namespace Vcs {

class Config;

void SetupPager(const Config& config);

bool PagerInUse() noexcept;

} // namespace Vcs
