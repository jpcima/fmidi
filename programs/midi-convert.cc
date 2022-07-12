//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "common.h"
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <io.h>
#define fileno _fileno
#define isatty _isatty
#endif

int main(int argc, char *argv[])
{
    if (argc != 2)
        return 1;

    const char *filename = argv[1];
    fmidi_smf_u smf(fmidi_auto_file_read(filename));

    if (!smf) {
        print_error();
        return 1;
    }

    if(isatty(fileno(stdout))) {
        fprintf(stderr, "Not writing binary data to the terminal.\n");
        return 1;
    }

    if (!fmidi_smf_stream_write(smf.get(), stdout)) {
        print_error();
        return 1;
    }

    return 0;
}
