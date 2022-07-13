//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <fmidi/fmidi.h>
#include <getopt.h>
#include <fts.h>
#include <sys/stat.h>
#include <boost/iostreams/filter/line.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <regex>
#include <iostream>
#include <string.h>
namespace io = boost::iostreams;

struct FTS_Deleter {
    void operator()(FTS *x) const noexcept
        { fts_close(x); }
};

class Pattern {
public:
    virtual ~Pattern() {}
    virtual bool match(const char *p, size_t n) const = 0;
};

class Matching_Filter : public io::basic_line_filter<char> {
    typedef io::basic_line_filter<char> base_type;
public:
    typedef typename base_type::char_type char_type;
    typedef typename base_type::category category;
    typedef std::char_traits<char_type> traits_type;
    typedef typename base_type::string_type string_type;

    explicit Matching_Filter(const Pattern &pat, const char *file_path, bool &has_match)
        : base_type(true),
          pat_(pat), file_path_(file_path), file_path_len_(strlen(file_path)),
          has_match_(has_match) {}

    string_type do_filter(const string_type &line) override
    {
        if (!pat_.match(line.data(), line.size()))
            return std::string();

        has_match_ = true;

        std::string result;
        result.reserve(file_path_len_ + line.size() + 3);
        result.append(file_path_, file_path_ + file_path_len_);
        result.push_back(':');
        result.push_back(' ');
        result.append(line);
        result.push_back('\n');
        return result;
    }

private:
    const Pattern &pat_;
    const char *file_path_ = nullptr;
    size_t file_path_len_ = 0;
    bool &has_match_;
};

static bool do_file(const char *path, const Pattern &pattern, bool &has_match)
{
    fmidi_smf_u smf(fmidi_smf_file_read(path));
    if (!smf)
        return false;

    io::filtering_ostream out;
    out.push(Matching_Filter(pattern, path, has_match));
    out.push(std::cout);

    out << *smf;
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
    std::cerr <<
        "Usage: fmidi-grep [options] <pattern> <input> [input...]\n"
        "  -r,-R   recursive\n"
        "  -E      extended pattern\n"
        "  -F      fixed string pattern\n"
        "";
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
