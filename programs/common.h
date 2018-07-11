//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <fmidi/fmidi.h>
#include <stdio.h>

inline void print_error()
{
    const fmidi_error_info_t *errinfo = fmidi_errinfo();
    const char *msg = fmidi_strerror(errinfo->code);
#if defined(FMIDI_DEBUG)
    fprintf(stderr, "%s:%d: %s\n", errinfo->file, errinfo->line, msg);
#else
    fprintf(stderr, "%s\n", msg);
#endif
}
