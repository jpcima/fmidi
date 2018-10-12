//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "playlist.h"
#include "fmidi/u_stdio.h"
#include <fmidi/fmidi.h>
#include <RtMidi.h>
#include <ev.h>
#include <curses.h>
#include <getopt.h>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
namespace stc = std::chrono;

static void vmessage(FILE *log, char level, const char *fmt, va_list ap)
{
    if (!log)
        return;

    std::time_t time = std::time(nullptr);
    char timebuf[32];
    if (time == (time_t)-1 || !ctime_r(&time, timebuf))
        throw std::runtime_error("time");
    if (char *p = strchr(timebuf, '\n'))
        *p = '\0';

    fprintf(log, "%s [%c] ", timebuf, level);
    vfprintf(log, fmt, ap);
    fputc('\n', log);
    fflush(log);
}

static void message(FILE *log, char level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmessage(log, level, fmt, ap);
    va_end(ap);
}

//
struct player_context {
    struct ev_loop *loop;
    const char *filename;
    fmidi_smf_t *smf;
    fmidi_player_t *plr;
    RtMidiOut *midiout;
    double duration;
    int speed;
    bool quit;
    bool play;
    bool looping;
    bool interrupt;
    Play_List *playlist = nullptr;
};

//
static void midi_reset(RtMidiOut &midiout)
{
    for (unsigned c = 0; c < 16; ++c) {
        // all sound off
        { uint8_t msg[] { (uint8_t)((0b1011 << 4) | c), 120, 0 };
            midiout.sendMessage(msg, sizeof(msg)); }
        // reset all controllers
        { uint8_t msg[] { (uint8_t)((0b1011 << 4) | c), 121, 0 };
            midiout.sendMessage(msg, sizeof(msg)); }
        // bank select
        { uint8_t msg[] { (uint8_t)((0b1011 << 4) | c), 0, 0 };
            midiout.sendMessage(msg, sizeof(msg)); }
        { uint8_t msg[] { (uint8_t)((0b1011 << 4) | c), 32, 0 };
            midiout.sendMessage(msg, sizeof(msg)); }
        // program change
        { uint8_t msg[] { (uint8_t)((0b1100 << 4) | c), 0 };
            midiout.sendMessage(msg, sizeof(msg)); }
        // pitch bend change
        { uint8_t msg[] { (uint8_t)((0b1110 << 4) | c), 0, 0b1000000 };
            midiout.sendMessage(msg, sizeof(msg)); }
    }
}

static void midi_sound_off(RtMidiOut &midiout)
{
    for (unsigned c = 0; c < 16; ++c) {
        // all sound off
        { uint8_t msg[] { (uint8_t)((0b1011 << 4) | c), 120, 0 };
            midiout.sendMessage(msg, sizeof(msg)); }
    }
}

static void sc55_text_insert(RtMidiOut &midiout, const char *text)
{
    uint8_t buf[256];
    size_t buflen = sizeof(buf);

    size_t textmax = buflen - 10;
    size_t textlen = strlen(text);
    textlen = (textlen < textmax) ? textlen : textmax;

    size_t index = 0;
    buf[index++] = 0xf0;
    buf[index++] = 0x41;  // manufacturer: Roland
    buf[index++] = 0x10;  // device ID: default

    buf[index++] = 0x45;  // model: SC-55
    buf[index++] = 0x12;  // mode: send

    size_t cs_start = index;
    buf[index++] = 0x10;  // address[0]
    buf[index++] = 0x00;  // address[1]
    buf[index++] = 0x00;  // address[2]
    // ASCII text
    for (size_t i = 0; i < textlen; ++i) {
        unsigned char c = text[i];
        buf[index++] = c & 127;
    }
    size_t cs_end = index;

    unsigned cs = 0;
    for (size_t i = cs_start; i < cs_end; ++i)
        cs += buf[i];
    cs = (128 - (cs & 127)) & 127;

    buf[index++] = cs;  // checksum
    buf[index++] = 0xf7;

    midiout.sendMessage(buf, index);
}

//
static void mvprintln(int row, int col, const char *fmt, ...)
{
    WINDOW *w = subwin(stdscr, 1, getmaxx(stdscr) - col, row, col);
    if (!w) return;
    va_list ap;
    va_start(ap, fmt);
    vw_printw(w, fmt, ap);
    va_end(ap);
    delwin(w);
}

//
static void update_status_display(player_context &ctx)
{
    fmidi_smf_t &smf = *ctx.smf;
    fmidi_player_t &plr = *ctx.plr;
    double timepos = fmidi_player_current_time(&plr);
    double duration = ctx.duration;

    unsigned tm = (unsigned)timepos / 60;
    unsigned ts = (unsigned)timepos % 60;
    unsigned dm = (unsigned)duration / 60;
    unsigned ds = (unsigned)duration % 60;

    const fmidi_smf_info_t *info = fmidi_smf_get_info(&smf);

    const char *filename = ctx.filename;

    clear();

    int row = 1;
    mvprintln(row++, 1, "FILE %s", filename);
    mvprintln(row++, 1, "TIME %02u:%02u / %02u:%02u", tm, ts, dm, ds);
    mvprintln(row++, 1, "SPEED %d%%", ctx.speed);
    attron(A_REVERSE);
    mvprintln(row, 1, "%s", ctx.play ? "PLAYING" : "PAUSED");
    mvprintln(row, 10, "%s", ctx.looping ? "LOOPING" : "");
    ++row;
    attroff(A_REVERSE);
    ++row;
    mvprintln(row++, 1, "FORMAT %u", info->format);
    mvprintln(row++, 1, "TRACKS %u", info->track_count);
    ++row;
    mvprintln(row++, 1,
              "[space] play/pause   [esc] quit"
              "   [pgup] previous file   [pgdn] next file");
    mvprintln(row++, 1,
              "[left] go -5s   [right] go +5s"
              "   [<] slower   [>] faster");
    mvprintln(row++, 1,
              "[home] rewind   [l] loop");

    refresh();
}

//
static void on_update_tick(struct ev_loop *loop, ev_timer *timer, int)
{
    player_context &ctx = *(player_context *)timer->data;

    update_status_display(ctx);
}

//
static void on_player_event(const fmidi_event_t *evt, void *cbdata)
{
    player_context &ctx = *(player_context *)cbdata;
    RtMidiOut &midiout = *ctx.midiout;

    if (evt->type == fmidi_event_message)
        midiout.sendMessage(evt->data, evt->datalen);
}

//
static void on_player_finish(void *cbdata)
{
    player_context &ctx = *(player_context *)cbdata;
    ev_break(ctx.loop, EVBREAK_ONE);
}

//
static void on_stdin(struct ev_loop *loop, ev_io *w, int revents)
{
    player_context &ctx = *(player_context *)w->data;
    fmidi_player_t &plr = *ctx.plr;

    int c = getch();

    switch (c) {
    case 27:  // escape
    case 3:   // console break
        ctx.quit = true;
        ev_break(loop, EVBREAK_ONE);
        break;

    case KEY_PPAGE:
        if (ctx.playlist->go_previous()) {
            ctx.interrupt = true;
            ev_break(loop, EVBREAK_ONE);
        }
        break;

    case KEY_NPAGE:
        if (ctx.playlist->go_next()) {
            ctx.interrupt = true;
            ev_break(loop, EVBREAK_ONE);
        }
        break;

    case KEY_HOME:
        fmidi_player_rewind(&plr);
        midi_reset(*ctx.midiout);
        update_status_display(ctx);
        break;

    case ' ':
        if (fmidi_player_running(&plr)) {
            fmidi_player_stop(&plr);
            ctx.play = false;
            midi_sound_off(*ctx.midiout);
        } else {
            fmidi_player_start(&plr);
            ctx.play = true;
        }
        update_status_display(ctx);
        break;

    case 'l': case 'L':
        ctx.looping = !ctx.looping;
        update_status_display(ctx);
        break;

    case KEY_LEFT: {
        double time = fmidi_player_current_time(&plr) - 5;
        fmidi_player_goto_time(&plr, (time < 0) ? 0 : time);
        update_status_display(ctx);
        break;
    }

    case KEY_RIGHT: {
        double time = fmidi_player_current_time(&plr) + 5;
        fmidi_player_goto_time(&plr, time);
        update_status_display(ctx);
        break;
    }

    case '<': {
        int speed = ctx.speed - 1;
        speed = ctx.speed = (speed < 1) ? 1 : speed;
        fmidi_player_set_speed(&plr, speed * 1e-2);
        update_status_display(ctx);
        break;
    }

    case '>': {
        int speed = ctx.speed + 1;
        speed = ctx.speed = (speed > 1000) ? 1000 : speed;
        fmidi_player_set_speed(&plr, speed * 1e-2);
        update_status_display(ctx);
        break;
    }
    }
}

//
int main(int argc, char *argv[])
{
    std::unique_ptr<Play_List> pl;
    bool random_play = false;
    const char *client_name = "fmidi";
    RtMidi::Api midi_api = RtMidi::UNSPECIFIED;
    FILE *playback_log = nullptr;
    unique_FILE playback_logfile;

    for (int c; (c = getopt(argc, argv, "rn:M:L:")) != -1; ) {
        switch (c) {
        case 'r':
            random_play = true;
            break;
        case 'n':
            client_name = optarg;
            break;
        case 'M':
            if (!strcmp(optarg, "alsa"))
                midi_api = RtMidi::LINUX_ALSA;
            else if (!strcmp(optarg, "jack"))
                midi_api = RtMidi::UNIX_JACK;
            break;
        case 'L':
            playback_log = fopen(optarg, "a");
            if (!playback_log) {
                fprintf(stderr, "Cannot open the log file for writing.\n");
                return 1;
            }
            playback_logfile.reset(playback_log);
            break;
        default:
            return 1;
        }
    }

    if (!random_play) {
        Linear_Play_List *lpl = new Linear_Play_List;
        pl.reset(lpl);
        for (int i = optind; i < argc; ++i)
            lpl->add_file(argv[i]);
    }
    else {
        Random_Play_List *rpl = new Random_Play_List;
        pl.reset(rpl);
        for (int i = optind; i < argc; ++i)
            rpl->add_file(argv[i]);
    }

    RtMidiOut midiout(midi_api, client_name);
    midiout.openVirtualPort("MIDI out");

    initscr();
    raw();
    keypad(stdscr, true);
    noecho();
    timeout(0);
    curs_set(0);

    struct ev_loop *loop = EV_DEFAULT;

    ev_io stdin_watcher;
    ev_io_init(&stdin_watcher, &on_stdin, 0, EV_READ);
    ev_io_start(loop, &stdin_watcher);

    ev_timer update_timer;
    ev_timer_init(&update_timer, &on_update_tick, 0.0, 0.5);
    ev_timer_start(loop, &update_timer);

    int speed = 100;
    bool play = false;
    bool looping = false;

    pl->start();
    while (!pl->at_end()) {
        std::string filename = pl->current();

        fmidi_smf_u smf(fmidi_auto_file_read(filename.c_str()));
        if (!smf) {
            const char *msg = fmidi_strerror(fmidi_errno());
            message(playback_log, 'E', "%s", msg);
            pl->go_next();
            continue;
        }

        fmidi_player_u plr(fmidi_player_new(smf.get(), loop));
        if (!plr) {
            const char *msg = fmidi_strerror(fmidi_errno());
            message(playback_log, 'E', "%s", msg);
            pl->go_next();
            continue;
        }

        message(playback_log, 'I', "play %s", filename.c_str());

        player_context ctx;
        ctx.loop = loop;
        ctx.filename = filename.c_str();
        ctx.smf = smf.get();
        ctx.plr = plr.get();
        ctx.midiout = &midiout;
        ctx.duration = fmidi_smf_compute_duration(smf.get());
        ctx.speed = speed;
        ctx.quit = false;
        ctx.play = play;
        ctx.looping = looping;
        ctx.interrupt = false;
        ctx.playlist = pl.get();

        stdin_watcher.data = &ctx;
        update_timer.data = &ctx;
        fmidi_player_event_callback(plr.get(), &on_player_event, &ctx);
        fmidi_player_finish_callback(plr.get(), &on_player_finish, &ctx);

        midi_reset(midiout);
        sc55_text_insert(midiout, filename.c_str());

        fmidi_player_set_speed(plr.get(), ctx.speed * 1e-2);
        if (play)
            fmidi_player_start(plr.get());

        update_status_display(ctx);
        ev_run(loop, 0);

        if (ctx.quit)
            break;

        if (!ctx.looping && !ctx.interrupt)
            pl->go_next();

        speed = ctx.speed;
        play = ctx.play;
        looping = ctx.looping;
    }

    midi_reset(midiout);
    std::this_thread::sleep_for(stc::seconds(1));

    endwin();

    return 0;
}
