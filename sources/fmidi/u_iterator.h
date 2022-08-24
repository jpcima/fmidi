#pragma once
#include <iterator>
#include <vector>
#include <cstddef>

class callback_insert_iterator : public std::iterator<std::output_iterator_tag, void, void, void, void> {
public:
    using container_type = std::vector<char>;
    using callback_type = void(const char *, std::size_t, void *);

    callback_insert_iterator(void *cookie, callback_type *callback, container_type &line_buffer);
    callback_insert_iterator &operator=(char value);
    callback_insert_iterator &operator*() { return *this; }
    callback_insert_iterator &operator++() { return *this; }
    callback_insert_iterator &operator++(int) { return *this; }

    // NOTE: always call this when finished
    void flush();

private:
    void *cookie_ = nullptr;
    callback_type *callback_ = nullptr;
    container_type *line_buffer_ = nullptr;
};
