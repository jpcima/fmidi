//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "playlist.h"
#if defined(FMIDI_PLAY_USE_BOOST_FILESYSTEM)
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
namespace sys = boost::system;
#else
#include <fts.h>
#endif
#include <memory>
#include <ctime>

#if !defined(FMIDI_PLAY_USE_BOOST_FILESYSTEM)
struct FTS_Deleter {
    void operator()(FTS *x) const
        { fts_close(x); }
};
typedef std::unique_ptr<FTS, FTS_Deleter> FTS_u;
#endif

//
void Linear_Play_List::add_file(const std::string &path)
{
    files_.emplace_back(path);
}

void Linear_Play_List::start()
{
    index_ = 0;
}

bool Linear_Play_List::at_end() const
{
    return index_ == files_.size();
}

const std::string &Linear_Play_List::current() const
{
    return files_[index_];
}

bool Linear_Play_List::go_next()
{
    if (index_ == files_.size())
        return false;
    ++index_;
    return true;
}

bool Linear_Play_List::go_previous()
{
    if (index_ == 0)
        return false;
    --index_;
    return true;
}

//
Random_Play_List::Random_Play_List()
    : prng_(std::time(nullptr))
{
}

void Random_Play_List::add_file(const std::string &path)
{
    scan_files(path);
}

void Random_Play_List::start()
{
    index_ = 0;
    history_.clear();
    if (!files_.empty())
        history_.push_back(&files_[random_file()]);
}

bool Random_Play_List::at_end() const
{
    return history_.empty();
}

const std::string &Random_Play_List::current() const
{
    return *history_[index_];
}

bool Random_Play_List::go_next()
{
    if (index_ < history_.size() - 1)
        ++index_;
    else {
        if (history_.size() == history_max)
            history_.pop_front();
        history_.push_back(&files_[random_file()]);
        index_ = history_.size() - 1;
    }
    return true;
}

bool Random_Play_List::go_previous()
{
    if (index_ == 0)
        return false;
    --index_;
    return true;
}

size_t Random_Play_List::random_file() const
{
    std::uniform_int_distribution<size_t> dist(0, files_.size() - 1);
    return dist(prng_);
}

#if !defined(FMIDI_PLAY_USE_BOOST_FILESYSTEM)
void Random_Play_List::scan_files(const std::string &path)
{
    char *path_argv[] = { (char *)path.c_str(), nullptr };

    FTS_u fts(fts_open(path_argv, FTS_NOCHDIR|FTS_LOGICAL, nullptr));
    if (!fts)
        return;

    while (FTSENT *ent = fts_read(fts.get())) {
        if (ent->fts_info == FTS_F)
            files_.emplace_back(ent->fts_path);
    }
}
#else
void Random_Play_List::scan_files(const std::string &path)
{
    sys::error_code ec;

    ec.clear();
    fs::file_status st = fs::status(path, ec);
    if (ec)
        return;

    switch (st.type()) {
    default:
        break;
    case fs::regular_file:
        files_.emplace_back(path);
        break;
    case fs::directory_file:
        ec.clear();
        fs::recursive_directory_iterator it(path, ec);
        if (ec)
            return;
        while (it != fs::recursive_directory_iterator()) {
            ec.clear();
            st = it->status(ec);
            if (ec || st.type() != fs::regular_file)
                continue;
            files_.emplace_back(it->path().string());
            ec.clear();
            it.increment(ec);
            if (ec)
                break;
        }
        break;
    }
}
#endif
