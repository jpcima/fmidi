//          Copyright Jean Pierre Cimalando 2018-2022.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "u_stdio.h"
#if defined(_WIN32)
# include <windows.h>
# include <memory>
# include <errno.h>
#endif

FILE *fmidi_fopen(const char *path, const char *mode)
{
#if !defined(_WIN32)
    return fopen(path, mode);
#else
    auto toWideString = [](const char *utf8) -> wchar_t * {
        unsigned wsize = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (wsize == 0)
            return nullptr;
        wchar_t *wide = new wchar_t[wsize];
        wsize = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wsize);
        if (wsize == 0) {
            delete[] wide;
            return nullptr;
        }
        return wide;
    };
    std::unique_ptr<wchar_t[]> wpath(toWideString(path));
    if (!wpath) {
        errno = EINVAL;
        return nullptr;
    }
    std::unique_ptr<wchar_t[]> wmode(toWideString(mode));
    if (!wmode) {
        errno = EINVAL;
        return nullptr;
    }
    return _wfopen(wpath.get(), wmode.get());
#endif
}
