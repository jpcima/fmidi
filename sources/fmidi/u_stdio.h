//          Copyright Jean Pierre Cimalando 2018-2022.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <memory>
#include <stdio.h>

////////////////////////
// FILE PATH ENCODING //
////////////////////////

FILE *fmidi_fopen(const char *path, const char *mode);

///////////////
// FILE RAII //
///////////////

struct FILE_deleter;
typedef std::unique_ptr<FILE, FILE_deleter> unique_FILE;

struct FILE_deleter {
    void operator()(FILE *stream) const
        { fclose(stream); }
};
