//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <string>
#include <vector>
#include <deque>
#include <random>

class Play_List {
public:
    virtual ~Play_List() {}
    virtual void start() = 0;
    virtual bool at_end() const = 0;
    virtual const std::string &current() const = 0;
    virtual bool go_next() = 0;
    virtual bool go_previous() = 0;
};

class Linear_Play_List : public Play_List {
public:
    void add_file(const std::string &path);
    void start() override;
    bool at_end() const override;
    const std::string &current() const override;
    bool go_next() override;
    bool go_previous() override;
private:
    std::vector<std::string> files_;
    size_t index_ = 0;
};

class Random_Play_List : public Play_List {
public:
    Random_Play_List();
    void add_file(const std::string &path);
    void start() override;
    bool at_end() const override;
    const std::string &current() const override;
    bool go_next() override;
    bool go_previous() override;
private:
    size_t random_file() const;
    void scan_files(const std::string &path);
    std::vector<std::string> files_;
    enum { history_max = 10 };
    std::deque<const std::string *> history_;
    size_t index_ = 0;
    mutable std::mt19937 prng_;
};
