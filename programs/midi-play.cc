#include <fmidi/fmidi.h>
#include <RtMidi.h>
#include <ev.h>
#include <curses.h>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
namespace stc = std::chrono;

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
    unsigned current;
    unsigned next;
    unsigned total;
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
    mvprintln(row++, 1, "%s", ctx.play ? "PLAYING" : "PAUSED");
    attroff(A_REVERSE);
    ++row;
    mvprintln(row++, 1, "FORMAT %u", info->format);
    mvprintln(row++, 1, "TRACKS %u", info->track_count);
    ++row;
    mvprintln(row++, 1,
              "[space] play/pause   [esc] quit"
              "   [pgup] previous file   [pgdn] next file");
    mvprintln(row++, 1,
              "[home] rewind   [left] go -5s   [right] go +5s"
              "   [<] slower   [>] faster");

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
        if (ctx.current > 0) {
            ctx.next = ctx.current - 1;
            ev_break(loop, EVBREAK_ONE);
            update_status_display(ctx);
        }
        break;

    case KEY_NPAGE:
        if (ctx.current < ctx.total - 1) {
            ctx.next = ctx.current + 1;
            ev_break(loop, EVBREAK_ONE);
            update_status_display(ctx);
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
    if (argc < 2)
        return 1;

    RtMidiOut midiout(RtMidi::UNSPECIFIED, "fmidi");
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

    char **files = argv + 1;
    unsigned nfiles = (unsigned)argc - 1;

    int speed = 100;
    bool play = false;

    for (unsigned i = 0; i < nfiles;) {
        const char *filename = files[i];

        fmidi_smf_u smf(fmidi_smf_file_read(filename));
        if (!smf) {
            // const char *msg = fmidi_strerror(fmidi_errno());
            ++i;
            continue;
        }

        fmidi_player_u plr(fmidi_player_new(smf.get(), loop));
        if (!plr) {
            // const char *msg = fmidi_strerror(fmidi_errno());
            ++i;
            continue;
        }

        player_context ctx;
        ctx.loop = loop;
        ctx.filename = filename;
        ctx.smf = smf.get();
        ctx.plr = plr.get();
        ctx.midiout = &midiout;
        ctx.duration = fmidi_smf_compute_duration(smf.get());
        ctx.speed = speed;
        ctx.quit = false;
        ctx.play = play;
        ctx.current = i;
        ctx.next = i + 1;
        ctx.total = nfiles;

        stdin_watcher.data = &ctx;
        update_timer.data = &ctx;
        fmidi_player_event_callback(plr.get(), &on_player_event, &ctx);
        fmidi_player_finish_callback(plr.get(), &on_player_finish, &ctx);

        midi_reset(midiout);

        fmidi_player_set_speed(plr.get(), ctx.speed * 1e-2);
        if (play)
            fmidi_player_start(plr.get());

        update_status_display(ctx);
        ev_run(loop, 0);

        if (ctx.quit)
            break;

        i = ctx.next;
        speed = ctx.speed;
        play = ctx.play;
    }

    midi_reset(midiout);

    endwin();

    return 0;
}
