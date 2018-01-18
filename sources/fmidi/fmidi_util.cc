#include "fmidi.h"
#include "fmidi_internal.h"
#include "u_memstream.h"
#include <fmt/format.h>
#include <fmt/ostream.h>

double fmidi_smpte_time(const fmidi_smpte *smpte)
{
    const uint8_t *d = smpte->code;
    static const double spftable[4] = { 1.0/24, 1.0/25, 1001.0/30000, 1.0/30 };
    uint8_t hh = d[0];
    double spf = spftable[(hh >> 5) & 0b11];
    hh &= 0b11111;
    uint8_t mm = d[1], ss = d[2], fr = d[3], ff = d[4];
    return (fr + 0.01 * ff) * spf + ss + mm * 60 + hh * 3600;
}

double fmidi_delta_time(double delta, uint16_t unit, uint32_t tempo)
{
    if (unit & (1 << 15)) {
        unsigned tpf = unit & 0xff;  // delta units per frame
        unsigned fps = -(int8_t)(unit >> 8);  // frames per second
        return delta / (tpf * fps);
    }
    else {
        unsigned dpqn = unit;  // delta units per 1/4 note
        double tpqn = 1e-6 * tempo;  // 1/4 note duration
        return delta * tpqn / dpqn;
    }
}

double fmidi_time_delta(double time, uint16_t unit, uint32_t tempo)
{
    if (unit & (1 << 15)) {
        unsigned tpf = unit & 0xff;  // delta units per frame
        unsigned fps = -(int8_t)(unit >> 8);  // frames per second
        return time * (tpf * fps);
    }
    else {
        unsigned dpqn = unit;  // delta units per 1/4 note
        double tpqn = 1e-6 * tempo;  // 1/4 note duration
        return time * dpqn / tpqn;
    }
}

//------------------------------------------------------------------------------
class fmidi_category_t : public std::error_category {
public:
    const char *name() const noexcept override
        { return "fmidi"; }
    std::string message(int condition) const override
        { return fmidi_strerror((fmidi_status_t)condition); }
};

static fmidi_category_t the_category;

const std::error_category &fmidi_category() {
    return the_category;
};

//------------------------------------------------------------------------------
static bool fmidi_repr_meta(std::ostream &out, const uint8_t *data, uint32_t len)
{
    if (len <= 0)
        return false;

    unsigned tag = *data++;
    --len;

    printfmt_quoted qtext{(const char *)data, len};

    switch (tag) {
    default:
        fmt::print(out, "(meta/unknown :tag #x{:x})", tag);
        return true;
    case 0x00: {  // sequence number
        if (len < 2) return false;
        unsigned number = (data[0] << 8) | data[1];
        fmt::print(out, "(meta/seq-number {})", number);
        return true;
    }
    case 0x01:
        fmt::print(out, "(meta/text {})", qtext);
        return true;
    case 0x02:
        fmt::print(out, "(meta/copyright {})", qtext);
        return true;
    case 0x03:
        fmt::print(out, "(meta/track {})", qtext);
        return true;
    case 0x04:
        fmt::print(out, "(meta/instrument {})", qtext);
        return true;
    case 0x05:
        fmt::print(out, "(meta/lyric {})", qtext);
        return true;
    case 0x06:
        fmt::print(out, "(meta/marker {})", qtext);
        return true;
    case 0x07:
        fmt::print(out, "(meta/cue-point {})", qtext);
        return true;
    case 0x20:
        if (len < 1) return false;
        fmt::print(out, "(meta/channel-prefix {})", data[0]);
        return true;
    case 0x21:
        if (len < 1) return false;
        fmt::print(out, "(meta/port {})", data[0]);
        return true;
    case 0x2f:
    case 0x3f:
        fmt::print(out, "(meta/end)");
        return true;
    case 0x51: {
        if (len < 3) return false;
        unsigned t = (data[0] << 16) | (data[1] << 8) | data[2];
        fmt::print(out, "(meta/tempo {} #|{} bpm|#)", t, 60. / (t * 1e-6));
        return true;
    }
    case 0x54: {
        if (len < 5) return false;
        static const char *fpstable[] = {"24", "25", "30000/1001", "30"};
        uint8_t hh = data[0];
        const char *fps = fpstable[(hh >> 5) & 0b11];
        fmt::print(
            out, "(meta/offset {:02d} {:02d} {:02d} {:02d} {:02d}/100 :frames/second {})",
            hh & 0b11111, data[1], data[2], data[3], data[4], fps);
        return true;
    }
    case 0x58:
        if (len < 4) return false;
        fmt::print(out, "(meta/time-sig {} {} {} {})",
                   data[0], data[1], data[2], data[3]);
        return true;
    case 0x59: {
        if (len < 2) return false;
        fmt::print(out, "(meta/key-sig {} :{})",
                   (int8_t)data[0], data[1] ? "minor" : "major");
        return true;
    }
    case 0x7f:
        fmt::print(out, "(meta/sequencer-specific {})", printfmt_bytes{data, len});
        return true;
    }

    return false;
}

static bool fmidi_repr_midi(std::ostream &out, const uint8_t *data, uint32_t len)
{
    if (len <= 0)
        return false;

    unsigned status = *data++;
    --len;

    auto b7 = [data](unsigned i)
        { return data[i] & 0x7f; };
    auto b14 = [data](unsigned i)
        { return (data[i] & 0x7f) | (data[i + 1] & 0x7f) << 7; };

    if (status >> 4 == 0xf) {
        unsigned op = status & 0xf;

        switch (op) {
        case 0b0000:
            fmt::print(out, "(sysex #xf0 {})", printfmt_bytes{data, len});
            return true;
        case 0b0001: {
            if (len < 1) return false;
            unsigned tc = b7(0);
            fmt::print(out, "(time-code {} {})", tc >> 4, tc & 0b1111);
            return true;
        }
        case 0b0010:
            if (len < 2) return false;
            fmt::print(out, "(song-position {})", b14(0));
            return true;
        case 0b0011:
            if (len < 1) return {};
            fmt::print(out, "(song-select {})", b7(0));
            return true;
        case 0b0110:
            fmt::print(out, "(tune-request)");
            return true;
        case 0b1000:
            fmt::print(out, "(timing-clock)");
            return true;
        case 0b1010:
            fmt::print(out, "(start)");
            return true;
        case 0b1011:
            fmt::print(out, "(continue)");
            return true;
        case 0b1100:
            fmt::print(out, "(stop)");
            return true;
        case 0b1110:
            fmt::print(out, "(active-sensing)");
            return true;
        case 0b1111:
            fmt::print(out, "(reset)");
            return true;
        }
    }
    else {
        unsigned op = status >> 4;
        unsigned ch = status & 0xf;

        switch (op) {
        case 0b1000:
            if (len < 2) return false;
            fmt::print(out, "(note-off {} :velocity {} :channel {})", b7(0), b7(1), ch);
            return true;
        case 0b1001:
            if (len < 2) return false;
            fmt::print(out, "(note-on {} :velocity {} :channel {})", b7(0), b7(1), ch);
            return true;
        case 0b1010:
            if (len < 2) return false;
            fmt::print(out, "(poly-aftertouch {} :pressure {} :channel {})", b7(0), b7(1), ch);
            return true;
        case 0b1011:
            if (len < 2) return false;
            fmt::print(out, "(control #x{:x} {} :channel {})", b7(0), b7(1), ch);
            return true;
        case 0b1100:
            if (len < 1) return false;
            fmt::print(out, "(program {} :channel {})", b7(0), ch);
            return true;
        case 0b1101:
            if (len < 1) return false;
            fmt::print(out, "(aftertouch :pressure {} :channel {})", b7(0), ch);
            return true;
        case 0b1110:
            if (len < 2) return false;
            fmt::print(out, "(pitch-bend {} :channel {})", b14(0), ch);
            return true;
        }
    }

    return false;
}

std::ostream &operator<<(std::ostream &out, const fmidi_smf_t &smf)
{
    const fmidi_smf_info_t *info = fmidi_smf_get_info(&smf);
    fmt::print(out, "(midi-file");
    fmt::print(out, "\n  :format {}", info->format);

    unsigned unit = info->delta_unit;
    if (unit & (1 << 15))
        fmt::print(out, "\n  :delta-unit (smpte-based :units/frame {} :frames/second {})",
                   unit & 0xff, -(int8_t)(unit >> 8));
    else
        fmt::print(out, "\n  :delta-unit (tempo-based :units/beat {})", unit);

    fmt::print(out, "\n  :tracks"
               "\n  (", unit);

    for (unsigned i = 0, n = info->track_count; i < n; ++i) {
        fmidi_track_iter_t it;
        fmidi_smf_track_begin(&it, i);
        if (i > 0)
            fmt::print(out, "\n   ");
        fmt::print(out, "(;;--- track {} ---;;", i);
        while (const fmidi_event_t *evt = fmidi_smf_track_next(&smf, &it))
            fmt::print(out, "\n    (:delta {:<5} {})", evt->delta, *evt);
        fmt::print(out, ")");
    }
    fmt::print(out, "))\n");
    return out;
}

std::ostream &operator<<(std::ostream &out, const fmidi_event_t &evt)
{
    const uint8_t *data = evt.data;
    uint32_t len = evt.datalen;

    switch (evt.type) {
    case fmidi_event_meta: {
        if (!fmidi_repr_meta(out, data, len))
            fmt::print(out, "(meta/unknown)");
        break;
    }
    case fmidi_event_message: {
        if (!fmidi_repr_midi(out, data, len))
            fmt::print(out, "(unknown)");
        break;
    }
    case fmidi_event_escape: {
        fmt::print(out, "(raw {})", printfmt_bytes{data, len});
        break;
    }
    }

    return out;
}

//------------------------------------------------------------------------------
std::ostream &operator<<(std::ostream &out, const printfmt_quoted &q)
{
    const char *text = q.text;
    size_t length = q.length;
    out.put('"');
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        if (c == '\\' || c == '"') out.put('\\');
        out.put(c);
    }
    return out.put('"');
}

std::ostream &operator<<(std::ostream &out, const printfmt_bytes &b)
{
    const uint8_t *data = b.data;
    for (size_t i = 0, n = b.size; i < n; ++i) {
        if (i > 0) out.put(' ');
        fmt::print(out, "#x{:02x}", data[i]);
    }
    return out;
}
