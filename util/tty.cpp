#include "tty.h"
#include "platform.h"

#if defined(_win_)
#   include <io.h>
#   define ISATTY _isatty
#   define FILENO _fileno
#else
#   include <unistd.h>
#   define ISATTY isatty
#   define FILENO fileno
#endif

namespace util {

bool is_atty(FILE* f) noexcept {
    return ISATTY(FILENO(f));
}

} // namespace util
