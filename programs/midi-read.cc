//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "common.h"
#include <iostream>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const char *filename = argv[i];
        fmidi_smf_u smf(fmidi_auto_file_read(filename));
        if (!smf) {
            print_error();
            return 1;
        }
        std::cout << *smf;
    }

    return 0;
}
