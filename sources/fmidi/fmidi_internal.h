#pragma once
#include "fmidi.h"

//------------------------------------------------------------------------------
struct printfmt_quoted {
    printfmt_quoted(const char *text, size_t length)
        : text(text), length(length) {}
    const char *text = nullptr;
    size_t length = 0;
};
std::ostream &operator<<(std::ostream &out, const printfmt_quoted &q);

//------------------------------------------------------------------------------
struct printfmt_bytes {
    printfmt_bytes(const uint8_t *data, size_t size)
        : data(data), size(size) {}
    const uint8_t *data = nullptr;
    size_t size = 0;
};
std::ostream &operator<<(std::ostream &out, const printfmt_bytes &b);
