//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <fmidi/fmidi.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <iostream>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const char *filename = argv[i];
        fmidi_smf_u smf(fmidi_smf_file_read(filename));
        if (!smf) {
            const fmidi_error_info_t *errinfo = fmidi_errinfo();
            const char *msg = fmidi_strerror(errinfo->code);
#if defined(FMIDI_DEBUG)
            fmt::print(std::cerr, "{}:{}: {}\n", errinfo->file, errinfo->line, msg);
#else
            fmt::print(std::cerr, "{}\n", msg);
#endif
            return 1;
        }
        std::cout << *smf;
    }

    return 0;
}
