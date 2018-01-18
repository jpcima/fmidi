#pragma once
#include <memory>
#include <stdio.h>

struct FILE_deleter;
typedef std::unique_ptr<FILE, FILE_deleter> unique_FILE;

struct FILE_deleter {
    void operator()(FILE *stream) const
        { fclose(stream); }
};
