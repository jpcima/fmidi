#pragma once
#include "fmidi.h"

//------------------------------------------------------------------------------
struct printfmt_quoted {
    const char *text = nullptr;
    size_t length = 0;
};
std::ostream &operator<<(std::ostream &out, const printfmt_quoted &q);

//------------------------------------------------------------------------------
struct printfmt_bytes {
    const uint8_t *data = nullptr;
    size_t size = 0;
};
std::ostream &operator<<(std::ostream &out, const printfmt_bytes &b);
