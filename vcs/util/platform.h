#pragma once

#if defined(__linux__)
#   define _linux_
#elif defined(_WIN64)
#   define _win64_
#   define _win32_
#elif defined(__WIN32__) || defined(_WIN32)
#   define _win32_
#elif defined(__APPLE__)
#   define _darwin_
#endif

#if defined(_win32_) || defined(_win64_)
#   define _win_
#endif

#if defined(_linux_) || defined(_darwin_)
#   define _unix_
#endif

#if defined(__x86_64__) || defined(_M_X64)
#   define _x86_64_
#endif

#if defined(__i386__) || defined(_M_IX86)
#   define _i386_
#endif

#if defined(_x86_64_)
#   define _64_
#else
#   define _32_
#endif

#if defined(_32_)
#   define PLATFORM_DATA_ALIGN 4
#elif defined(_64_)
#   define PLATFORM_DATA_ALIGN 8
#endif
