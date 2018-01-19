#include "fmidi.h"
#include "fmidi_internal.h"
#include "u_memstream.h"
#include "u_stdio.h"
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
# define fileno _fileno
#endif

static thread_local fmidi_error_info_t fmidi_last_error;

#if defined(FMIDI_DEBUG)
# define RET_FAIL(x, e) do {                                      \
        fmidi_error_info_t &fmidi__err = fmidi_last_error;        \
        fmidi__err.file = __FILE__; fmidi__err.line = __LINE__;   \
        fmidi__err.code = (e); return (x); } while (0)
#else
# define RET_FAIL(x, e)                                         \
    do { fmidi_last_error.code = (e); return (x); } while (0)
#endif

struct fmidi_raw_track {
    std::unique_ptr<uint8_t[]> data;
    uint32_t length;
};

struct fmidi_smf {
    fmidi_smf_info_t info;
    std::unique_ptr<fmidi_raw_track[]> track;
};

const fmidi_smf_info_t *fmidi_smf_get_info(const fmidi_smf_t *smf)
{
    return &smf->info;
}

double fmidi_smf_compute_duration(const fmidi_smf_t *smf)
{
    double duration = 0;
    fmidi_seq_u seq(fmidi_seq_new(smf));
    fmidi_seq_event_t sqevt;
    while (fmidi_seq_next_event(seq.get(), &sqevt))
        duration = sqevt.time;
    return duration;
}

static uintptr_t fmidi_event_pad(uintptr_t size)
{
    uintptr_t nb = size % alignof(fmidi_event_t);
    return nb ? (size + alignof(fmidi_event_t) - nb) : size;
}

static fmidi_event_t *fmidi_event_alloc(std::vector<uint8_t> &buf, uint32_t datalen)
{
    size_t pos = buf.size();
    size_t evsize = fmidi_event_sizeof(datalen);
    size_t padsize = fmidi_event_pad(evsize);
    buf.resize(buf.size() + padsize);
    fmidi_event_t *event = (fmidi_event_t *)&buf[pos];
    return event;
}

static unsigned fmidi_message_sizeof(uint8_t id)
{
    if ((id >> 7) == 0) {
        return 0;
    }
    else if ((id >> 4) != 0b1111) {
        static const uint8_t sizetable[8] = {
            3, 3, 3, 3, 2, 2, 3 };
        return sizetable[(id >> 4) & 0b111];
    }
    else {
        static const uint8_t sizetable[16] = {
            0, 2, 3, 2, 1, 1, 1, 0,
            1, 1, 1, 1, 1, 1, 1, 1 };
        return sizetable[id & 0b1111];
    }
}

static fmidi_event_t *fmidi_read_meta_event(
    memstream &mb, std::vector<uint8_t> &evbuf, uint32_t delta)
{
    memstream_status ms;
    unsigned id;
    if ((ms = mb.readbyte(&id)))
        RET_FAIL(nullptr, (fmidi_status)ms);

    uint32_t datalen;
    const uint8_t *data;
    if (id == 0x2f || id == 0x3f) {  // end of track
        if (mb.skipbyte(0)) {
            // omitted final null byte in some broken files
        }
        else {
            // repeated end of track events
            for (bool again = true; again;) {
                size_t offset = mb.getpos();
                again = !mb.readvlq(nullptr) && !mb.skipbyte(0xff) &&
                    (!mb.skipbyte(0x2f) || !mb.skipbyte(0x3f));
                if (!again)
                    mb.setpos(offset);
                else
                    again = !mb.skipbyte(0);
            }
        }
        datalen = 0;
        data = nullptr;
    }
    else {
        if ((ms = mb.readvlq(&datalen)))
            RET_FAIL(nullptr, (fmidi_status)ms);
        if (!(data = mb.read(datalen)))
            RET_FAIL(nullptr, fmidi_err_eof);
    }
    fmidi_event_t *evt = fmidi_event_alloc(evbuf, datalen + 1);
    evt->type = fmidi_event_meta;
    evt->delta = delta;
    evt->datalen = datalen + 1;
    evt->data[0] = id;
    memcpy(&evt->data[1], data, datalen);
    return evt;
}

static fmidi_event_t *fmidi_read_escape_event(
    memstream &mb, std::vector<uint8_t> &evbuf, uint32_t delta)
{
    memstream_status ms;
    uint32_t datalen;
    const uint8_t *data;
    if ((ms = mb.readvlq(&datalen)))
        RET_FAIL(nullptr, (fmidi_status)ms);
    if (!(data = mb.read(datalen)))
        RET_FAIL(nullptr, fmidi_err_eof);

    fmidi_event_t *evt = fmidi_event_alloc(evbuf, datalen);
    evt->type = fmidi_event_escape;
    evt->delta = delta;
    evt->datalen = datalen;
    memcpy(&evt->data[0], data, datalen);
    return evt;
}

static fmidi_event_t *fmidi_read_sysex_event(
    memstream &mb, std::vector<uint8_t> &evbuf, uint32_t delta)
{
    memstream_status ms;
    fmidi_event_t *evt;

    std::vector<uint8_t> syxbuf;
    syxbuf.reserve(256);
    syxbuf.push_back(0xf0);

    uint32_t partlen;
    const uint8_t *part;
    if ((ms = mb.readvlq(&partlen)))
        RET_FAIL(nullptr, (fmidi_status)ms);
    if (!(part = mb.read(partlen)))
        RET_FAIL(nullptr, fmidi_err_eof);

    bool term = false;
    const uint8_t *endp;

    // handle files having multiple concatenated sysex events in one
    while ((endp = (const uint8_t *)memchr(part, 0xf7, partlen))) {
        syxbuf.insert(syxbuf.end(), part, endp + 1);

        evt = fmidi_event_alloc(evbuf, syxbuf.size());
        evt->type = fmidi_event_message;
        evt->delta = delta;
        evt->datalen = syxbuf.size();
        memcpy(&evt->data[0], &syxbuf[0], syxbuf.size());

        uint32_t reallen = endp + 1 - part;
        partlen -= reallen;
        part += reallen;

        if (partlen == 0)
            return evt;

        if (part[0] != 0xf0) {
#if 1
            // trailing garbage, ignore
#else
            // sierra: incorrect length covering part of the next event. repair
            mb.setpos(mb.getpos() - partlen);
#endif
            return evt;
        }
        ++part;
        --partlen;

        syxbuf.clear();
        syxbuf.push_back(0xf0);
    }

    // handle the rest in multiple parts (Casio MIDI)
    while (!term) {
        term = endp;
        if (term && endp + 1 != part + partlen) {
            // ensure no excess bytes
            RET_FAIL(nullptr, fmidi_err_format);
        }
        syxbuf.insert(syxbuf.end(), part, part + partlen);

        if (!term) {
            size_t offset = mb.getpos();
            bool havecont = false;

            uint32_t contdelta;
            unsigned id;
            if (!mb.readvlq(&contdelta) && !mb.readbyte(&id)) {
                // raw sequence incoming? use it as next sysex part
                havecont = id == 0xf7;
            }
            if (havecont) {
                if ((ms = mb.readvlq(&partlen)))
                    RET_FAIL(nullptr, (fmidi_status)ms);
                if (!(part = mb.read(partlen)))
                    RET_FAIL(nullptr, fmidi_err_eof);
                endp = (const uint8_t *)memchr(part, 0xf7, partlen);
            }
            else {
                // no next part? assume unfinished message and repair
                mb.setpos(offset);
                syxbuf.push_back(0xf7);
                term = true;
            }
        }
    }

    evt = fmidi_event_alloc(evbuf, syxbuf.size());
    evt->type = fmidi_event_message;
    evt->delta = delta;
    evt->datalen = syxbuf.size();
    memcpy(&evt->data[0], &syxbuf[0], syxbuf.size());
    return evt;
}

static fmidi_event_t *fmidi_read_message_event(
    memstream &mb, std::vector<uint8_t> &evbuf, unsigned id, uint32_t delta)
{
    uint32_t datalen = fmidi_message_sizeof(id);
    const uint8_t *data;
    if (datalen <= 0)
        RET_FAIL(nullptr, fmidi_err_format);
    if (!(data = mb.read(datalen - 1)))
        RET_FAIL(nullptr, fmidi_err_eof);

    fmidi_event_t *evt = fmidi_event_alloc(evbuf, datalen);
    evt->type = fmidi_event_message;
    evt->delta = delta;
    evt->datalen = datalen;
    evt->data[0] = id;
    memcpy(&evt->data[1], data, datalen - 1);
    return evt;
}

static fmidi_event_t *fmidi_read_event(
    memstream &mb, std::vector<uint8_t> &evbuf, uint8_t *runstatus)
{
    memstream_status ms;
    uint32_t delta;
    unsigned id;
    if ((ms = mb.readvlq(&delta)))
        RET_FAIL(nullptr, (fmidi_status)ms);
    if ((ms = mb.readbyte(&id)))
        RET_FAIL(nullptr, (fmidi_status)ms);

    fmidi_event_t *evt;
    if (id == 0xff) {
        evt = fmidi_read_meta_event(mb, evbuf, delta);
    }
    else if (id == 0xf7) {
        evt = fmidi_read_escape_event(mb, evbuf, delta);
    }
    else if (id == 0xf0) {
        evt = fmidi_read_sysex_event(mb, evbuf, delta);
    }
    else {
        if (id & 128) {
            *runstatus = id;
        }
        else {
            id = *runstatus;
            mb.setpos(mb.getpos() - 1);
        }
        evt = fmidi_read_message_event(mb, evbuf, id, delta);
    }

    return evt;
}

void fmidi_smf_track_begin(fmidi_track_iter_t *it, uint16_t track)
{
    it->track = track;
    it->index = 0;
}

const fmidi_event_t *fmidi_smf_track_next(
    const fmidi_smf_t *smf, fmidi_track_iter_t *it)
{
    if (it->track >= smf->info.track_count)
        return nullptr;

    const fmidi_raw_track &trk = smf->track[it->track];
    const uint8_t *trkdata = trk.data.get();

    const fmidi_event_t *evt = (const fmidi_event_t *)&trkdata[it->index];
    if ((const uint8_t *)evt == trkdata + trk.length)
        return nullptr;

    it->index += fmidi_event_pad(fmidi_event_sizeof(evt->datalen));
    return evt;
}

static bool fmidi_smf_read_contents(fmidi_smf_t *smf, memstream &mb)
{
    uint16_t ntracks = smf->info.track_count;
    smf->track = std::make_unique<fmidi_raw_track[]>(ntracks);

    std::vector<uint8_t> evbuf;
    evbuf.reserve(8192);

    uint8_t runstatus = 0;  // status runs from track to track

    for (unsigned itrack = 0; itrack < ntracks; ++itrack) {
        fmidi_raw_track &trk = smf->track[itrack];
        size_t trkoffset = mb.getpos();

        memstream_status ms;
        const uint8_t *trackmagic;
        uint32_t tracklen;

        if (!(trackmagic = mb.read(4))) {
            // file has less tracks than promised, repair
            smf->info.track_count = ntracks = itrack;
            break;
        }

        if (memcmp(trackmagic, "MTrk", 4)) {
            if (mb.getpos() == mb.endpos()) {
                // some kind of final junk header, ignore
                smf->info.track_count = ntracks = itrack;
                break;
            }
            RET_FAIL(false, fmidi_err_format);
        }
        if ((ms = mb.readint(&tracklen, 4)))
            RET_FAIL(false, (fmidi_status)ms);

        // check track length, broken in many files. disregard if invalid
        bool tracklengood = !mb.skip(tracklen) &&
            (mb.getpos() == mb.endpos() ||
             ((trackmagic = mb.peek(4)) && !memcmp(trackmagic, "MTrk", 4)));
        mb.setpos(trkoffset + 8);

        fmidi_event_t *evt;
        size_t evoffset = mb.getpos();
        bool endoftrack = false;
        evbuf.clear();
        while (!endoftrack && (evt = fmidi_read_event(mb, evbuf, &runstatus))) {
            // some files use 3F instead or 2F for end of track
            endoftrack = evt->type == fmidi_event_meta &&
                (evt->data[0] == 0x2f || evt->data[0] == 0x3f);
            // fmt::print(stderr, "T{} @{:#x} {}\n", itrack, evoffset, *evt);
            evoffset = mb.getpos();
            if (tracklengood && evoffset > trkoffset + 8 + tracklen)
                // next track overlap
                RET_FAIL(false, fmidi_err_format);
        }

        if (!endoftrack) {
            switch (fmidi_last_error.code) {
            case fmidi_err_eof:
                // truncated track? stop reading
                smf->info.track_count = ntracks = itrack + 1;
                break;
            case fmidi_err_format:
                // event with absurdly high delta time? ignore the rest of
                // the track and if possible proceed to the next
                mb.setpos(evoffset);
                if (mb.peekvlq(nullptr) == ms_err_format) {
                    if (!tracklengood)
                        smf->info.track_count = ntracks = itrack + 1;
                    break;
                }
                return false;
            default:
                return false;
            }
        }

        if (endoftrack) {
            // permit meta events coming after end of track
            const uint8_t *head;
            while ((head = mb.peek(2)) && head[0] == 0x00 && head[1] == 0xff) {
                if (!(evt = fmidi_read_event(mb, evbuf, &runstatus))) {
                    if (fmidi_last_error.code == fmidi_err_eof)
                        smf->info.track_count = ntracks = itrack + 1;
                    else
                        return false;
                }
                else if (tracklengood && mb.getpos() > trkoffset + 8 + tracklen)
                    // next track overlap
                    RET_FAIL(false, fmidi_err_format);
            }
        }

        uint32_t evdatalen = trk.length = evbuf.size();
        uint8_t *evdata = new uint8_t[evdatalen];
        trk.data.reset(evdata);
        memcpy(evdata, evbuf.data(), evdatalen);

        if (tracklengood)
            mb.setpos(trkoffset + 8 + tracklen);
    }

    return true;
}

fmidi_smf_t *fmidi_smf_mem_read(const uint8_t *data, size_t length)
{
    memstream mb(data, length);
    memstream_status ms;
    const uint8_t *filemagic;
    uint32_t headerlen;
    uint32_t format;
    uint32_t ntracks;
    uint32_t deltaunit;

    while ((filemagic = mb.peek(4)) && memcmp(filemagic, "MThd", 4))
        mb.skip(1);
    mb.skip(4);

    if (!filemagic)
        RET_FAIL(nullptr, fmidi_err_format);

    if ((ms = mb.readint(&headerlen, 4)) ||
        (ms = mb.readint(&format, 2)) ||
        (ms = mb.readint(&ntracks, 2)) ||
        (ms = mb.readint(&deltaunit, 2)))
        RET_FAIL(nullptr, (fmidi_status)ms);

    if (ntracks < 1 || headerlen < 6)
        RET_FAIL(nullptr, fmidi_err_format);

    if ((ms = mb.skip(headerlen - 6)))
        RET_FAIL(nullptr, (fmidi_status)ms);

    auto smf = std::make_unique<fmidi_smf_t>();
    smf->info.format = format;
    smf->info.track_count = ntracks;
    smf->info.delta_unit = deltaunit;

    if (!fmidi_smf_read_contents(smf.get(), mb))
        return nullptr;

    return smf.release();
}

void fmidi_smf_free(fmidi_smf_t *smf)
{
    delete smf;
}

fmidi_smf_t *fmidi_smf_file_read(const char *filename)
{
    unique_FILE fh(fopen(filename, "rb"));
    if (!fh)
        RET_FAIL(nullptr, fmidi_err_input);

    fmidi_smf_t *smf = fmidi_smf_stream_read(fh.get());
    return smf;
}

fmidi_smf_t *fmidi_smf_stream_read(FILE *stream)
{
    struct stat st;
    size_t length;

    if (fstat(fileno(stream), &st) != 0)
        RET_FAIL(nullptr, fmidi_err_input);

    length = st.st_size;
    if (length > fmidi_file_size_limit)
        RET_FAIL(nullptr, fmidi_err_largefile);

    auto buf = std::make_unique<uint8_t[]>(length);
    if (!fread(buf.get(), length, 1, stream))
        RET_FAIL(nullptr, fmidi_err_input);

    fmidi_smf_t *smf = fmidi_smf_mem_read(buf.get(), length);
    return smf;
}

void fmidi_smf_describe(const fmidi_smf_t *smf, FILE *stream)
{
#if defined(FMIDI_USE_BOOST)
    FILE_stream outs(stream);
    outs << *smf;
#else
    fmt::print(stream, "{}", *smf);
#endif
}

void fmidi_event_describe(const fmidi_event_t *evt, FILE *stream)
{
#if defined(FMIDI_USE_BOOST)
    FILE_stream outs(stream);
    outs << *evt;
#else
    fmt::print(stream, "{}", *evt);
#endif
}

fmidi_status_t fmidi_errno()
{
    return fmidi_last_error.code;
}

const fmidi_error_info_t *fmidi_errinfo()
{
    return &fmidi_last_error;
}

const char *fmidi_strerror(fmidi_status_t status)
{
    switch (status) {
    case fmidi_ok: return "success";
    case fmidi_err_format: return "invalid format";
    case fmidi_err_eof: return "premature end of file";
    case fmidi_err_input: return "input error";
    case fmidi_err_largefile: return "file too large";
    }
    return nullptr;
}
