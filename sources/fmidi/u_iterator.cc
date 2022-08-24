#include "u_iterator.h"
#include <cassert>

callback_insert_iterator::callback_insert_iterator(void *cookie, callback_type *callback, container_type &line_buffer)
    : cookie_{cookie}, callback_{callback}, line_buffer_{&line_buffer}
{
    assert(callback_);
}

callback_insert_iterator &callback_insert_iterator::operator=(char value)
{
    line_buffer_->push_back(value);
    if (value == '\n')
        flush();
    return *this;
}

void callback_insert_iterator::flush()
{
    container_type &buf = *line_buffer_;
    if (!buf.empty()) {
        callback_(buf.data(), buf.size(), cookie_);
        buf.clear();
    }
}
