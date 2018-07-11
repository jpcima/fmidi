//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "common.h"
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <memory>
#include <iostream>

int main(int argc, char *argv[])
{
    if (argc != 2)
        return 1;

    fmidi_smf_u smf(fmidi_auto_file_read(argv[1]));
    if (!smf) {
        print_error();
        return 1;
    }

    fmidi_seq_u seq(fmidi_seq_new(smf.get()));
    if (!seq) {
        print_error();
        return 1;
    }

    fmt::print("(midi-sequence");
    fmidi_seq_event_t plevt;
    while (fmidi_seq_next_event(seq.get(), &plevt)) {
        const fmidi_event_t *evt = plevt.event;
        fmt::print(std::cout, "\n  ({:<3} {:<12.6f} {})",
                   plevt.track, plevt.time, *evt);
    }
    fmt::print(")\n");

    return 0;
}
