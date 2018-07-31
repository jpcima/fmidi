//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <fmidi/fmidi.h>
#include <getopt.h>
#include <fts.h>
#include <sys/stat.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <regex>
#include <iostream>
#include <string.h>

struct FTS_Deleter {
    void operator()(FTS *x) const noexcept
        { fts_close(x); }
};

class Pattern {
public:
    virtual ~Pattern() {}
    virtual bool match(const char *p, size_t n) const = 0;
};

static bool do_file(const char *path, const Pattern &pattern, bool &has_match)
{
    fmidi_smf_u smf(fmidi_smf_file_read(path));

    if (!smf)
        return false;

    unsigned ntracks = fmidi_smf_get_info(smf.get())->track_count;

    for (unsigned t = 0; t < ntracks; ++t) {
        fmidi_track_iter_t it;
        fmidi_smf_track_begin(&it, t);

        size_t evt_num = 0;
        for (const fmidi_event_t *evt;
             (evt = fmidi_smf_track_next(smf.get(), &it)); ++evt_num) {
            std::string text = fmt::format("{}", *evt);
            if (pattern.match(text.data(), text.size())) {
                fmt::print(std::cout, "{}: [T{} E{}] {}\n", path, t + 1, evt_num + 1, text);
                has_match = true;
            }
        }
    }

    return true;
}

static bool do_tree(const char *path, const Pattern &pattern, bool &has_match)
{
    bool success = true;

    char *const path_argv[2] = {(char *)path, nullptr};
    std::unique_ptr<FTS, FTS_Deleter> fts(fts_open(path_argv, FTS_LOGICAL|FTS_NOCHDIR, nullptr));

    if (!fts)
        return false;

    while (FTSENT *ent = fts_read(fts.get())) {
        if (S_ISREG(ent->fts_statp->st_mode))
            success &= do_file(ent->fts_path, pattern, has_match);
    }

    return success;
}

class Grep_Pattern : public Pattern {
public:
    explicit Grep_Pattern(const char *pattern)
        : re_(pattern, std::regex::grep) {}
    bool match(const char *p, size_t n) const override
        { return std::regex_search(p, p + n, re_); }
private:
    std::regex re_;
};

class EGrep_Pattern : public Pattern {
public:
    explicit EGrep_Pattern(const char *pattern)
        : re_(pattern, std::regex::egrep) {}
    bool match(const char *p, size_t n) const override
        { return std::regex_search(p, p + n, re_); }
private:
    std::regex re_;
};

class Text_Pattern : public Pattern {
public:
    explicit Text_Pattern(const char *pattern)
        : pat_(pattern), patlen_(strlen(pattern)) {}
    bool match(const char *p, size_t n) const override
        { return memmem(p, n, pat_, patlen_) != nullptr; }
private:
    const char *pat_ = nullptr;
    size_t patlen_ = 0;
};

void usage()
{
    fmt::print(
        std::cerr,
        "Usage: fmidi-grep [options] <pattern> <input> [input...]\n"
        "  -r,-R   recursive\n"
        "  -E      extended pattern\n"
        "  -F      fixed string pattern\n"
        "");
}

int main(int argc, char *argv[])
{
    bool recurse = false;
    unsigned pattern_mode = std::regex::grep;

    for (int c; (c = getopt(argc, argv, "rREFh")) != -1;) {
        switch (c) {
        case 'r': case 'R':
            recurse = true; break;
        case 'E':
            pattern_mode = std::regex::egrep; break;
        case 'F':
            pattern_mode = 0; break;
        case 'h':
            usage(); return 0;
        default:
            usage(); return 1;
        }
    }

    if (argc - optind < 2) {
        usage();
        return 1;
    }

    bool success = true;

    const char *pattern = argv[optind];
    const char **inputs = (const char **)&argv[optind + 1];
    unsigned num_inputs = argc - optind - 1;

    std::unique_ptr<Pattern> pat;
    switch (pattern_mode) {
    case std::regex::grep:
        pat.reset(new Grep_Pattern(pattern)); break;
    case std::regex::egrep:
        pat.reset(new EGrep_Pattern(pattern)); break;
    default:
        pat.reset(new Text_Pattern(pattern)); break;
    }

    bool has_match = false;
    for (unsigned i = 0; i < num_inputs; ++i) {
        const char *input = inputs[i];
        success &= (recurse ? do_tree : do_file)(input, *pat, has_match);
    }
    if (!has_match)
        success = false;

    return success ? 0 : 1;
}
