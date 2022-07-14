//          Copyright Jean Pierre Cimalando 2018-2022.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "fmidi/fmidi.h"
#include "fmidi/fmidi_util.h"
#include "fmidi/fmidi_internal.h"
#include "fmidi/u_memstream.h"
#include "fmidi/u_stdio.h"
#include <string.h>

fmidi_smf_t *fmidi_mus_mem_read(const uint8_t *data, size_t length)
{
    const uint8_t magic[] = {'M', 'U', 'S', 0x1a};

    if (length < sizeof(magic) || memcmp(data, magic, 4))
        RET_FAIL(nullptr, fmidi_err_format);

    memstream mb(data + sizeof(magic), length - sizeof(magic));
    memstream_status ms;

    uint32_t score_len;
    uint32_t score_start;
    uint32_t channels;
    uint32_t sec_channels;
    uint32_t instr_cnt;

    if ((ms = mb.readintLE(&score_len, 2)) ||
        (ms = mb.readintLE(&score_start, 2)) ||
        (ms = mb.readintLE(&channels, 2)) ||
        (ms = mb.readintLE(&sec_channels, 2)) ||
        (ms = mb.readintLE(&instr_cnt, 2)) ||
        (ms = mb.skip(2)))
        RET_FAIL(nullptr, fmidi_err_format);

    std::unique_ptr<uint32_t[]> instrs{new uint32_t[instr_cnt]};
    for (uint32_t i = 0; i < instr_cnt; ++i) {
        if ((ms = mb.readintLE(&instrs[i], 2)))
            RET_FAIL(nullptr, fmidi_err_format);
    }

    fmidi_smf_u smf(new fmidi_smf);
    smf->info.format = 0;
    smf->info.track_count = 1;
    smf->info.delta_unit = 70; // DMX 140 Hz -> PPQN at 120 BPM
    smf->track.reset(new fmidi_raw_track[1]);

    fmidi_raw_track &track = smf->track[0];
    std::vector<uint8_t> evbuf;
    evbuf.reserve(8192);

    uint32_t ev_delta = 0;
    uint32_t note_velocity[16] = {};

    for (unsigned channel = 0; channel < 16; ++channel) {
        // initial velocity
        note_velocity[channel] = 64;
        // channel volume
        fmidi_event_t *event = fmidi_event_alloc(evbuf, 3);
        event->type = fmidi_event_message;
        event->delta = ev_delta;
        event->datalen = 3;
        uint8_t *data = event->data;
        data[0] = 0xb0 | channel;
        data[1] = 7;
        data[2] = 127;
    }

    for (bool score_end = false; !score_end;) {
        uint32_t ev_desc;
        if ((ms = mb.readintLE(&ev_desc, 1)))
            RET_FAIL(nullptr, fmidi_err_format);

        const uint8_t mus_channel_to_midi_channel[16] = {
            0,  1,  2,  3,  4,  5,  6,  7,
            8,  10, 11, 12, 13, 14, 15, 9
        };

        bool ev_last = (ev_desc & 128) != 0;
        uint32_t ev_type = (ev_desc >> 4) & 7;
        uint32_t ev_channel = mus_channel_to_midi_channel[ev_desc & 15];

        uint8_t midi[3] {};
        uint8_t midi_size = 0;

        switch (ev_type) {
            // release note
        case 0: {
            uint32_t data1;
            if ((ms = mb.readintLE(&data1, 1)))
                RET_FAIL(nullptr, fmidi_err_format);
            midi[0] = 0x80 | ev_channel;
            midi[1] = data1 & 127;
            midi[2] = 64;
            midi_size = 3;
            break;
        }
            // play note
        case 1: {
            uint32_t data1;
            if ((ms = mb.readintLE(&data1, 1)))
                RET_FAIL(nullptr, fmidi_err_format);
            if (data1 & 128) {
                uint32_t data2;
                if ((ms = mb.readintLE(&data2, 1)))
                    RET_FAIL(nullptr, fmidi_err_format);
                note_velocity[ev_channel] = data2 & 127;
            }
            midi[0] = 0x90 | ev_channel;
            midi[1] = data1 & 127;
            midi[2] = note_velocity[ev_channel];
            midi_size = 3;
            break;
        }
            // pitch wheel
        case 2: {
            uint32_t data1;
            if ((ms = mb.readintLE(&data1, 1)))
                RET_FAIL(nullptr, fmidi_err_format);
            uint32_t bend = (data1 < 128) ? (data1 << 6) :
                (8192 + (data1 - 128) * 8191 / 127);
            midi[0] = 0xe0 | ev_channel;
            midi[1] = bend & 127;
            midi[2] = bend >> 7;
            midi_size = 3;
            break;
        }
            // system event
        case 3: {
            uint32_t data1;
            if ((ms = mb.readintLE(&data1, 1)))
                RET_FAIL(nullptr, fmidi_err_format);
            midi[0] = 0xb0 | ev_channel;
            midi[2] = 0;
            midi_size = 3;
            switch (data1 & 127) {
            case 10: midi[1] = 120; break;
            case 11: midi[1] = 123; break;
            case 12: midi[1] = 126; break;
            case 13: midi[1] = 127; break;
            case 14: midi[1] = 121; break;
            default: midi_size = 0; break;
            }
            break;
        }
            // change controller
        case 4: {
            uint32_t data1;
            if ((ms = mb.readintLE(&data1, 1)))
                RET_FAIL(nullptr, fmidi_err_format);
            uint32_t data2;
            if ((ms = mb.readintLE(&data2, 1)))
                RET_FAIL(nullptr, fmidi_err_format);
            midi[0] = 0xb0 | ev_channel;
            midi[2] = data2 & 127;
            midi_size = 3;
            switch (data1 & 127) {
            case 0:
                // program change
                midi[0] = 0xc0 | ev_channel;
                midi[1] = data2 & 127;
                midi_size = 2;
                break;
            case 1: midi[1] = 0; break;
            case 2: midi[1] = 1; break;
            case 3: midi[1] = 7; break;
            case 4: midi[1] = 10; break;
            case 5: midi[1] = 11; break;
            case 6: midi[1] = 91; break;
            case 7: midi[1] = 93; break;
            case 8: midi[1] = 64; break;
            case 9: midi[1] = 67; break;
            default: midi_size = 0; break;
            }
            break;
        }
            // end of measure
        case 5: {
            break;
        }
            // score end
        case 6: {
            score_end = true;
            break;
        }
            // unknown purpose
        case 7: {
            if ((ms = mb.skip(1)))
                RET_FAIL(nullptr, fmidi_err_format);
            break;
        }
        }

        uint32_t delta_inc = 0;
        if (ev_last) {
            if ((ms = mb.readvlq(&delta_inc)))
                RET_FAIL(nullptr, fmidi_err_format);
            ev_desc += delta_inc;
        }

        if (midi_size > 0) {
            fmidi_event_t *event = fmidi_event_alloc(evbuf, midi_size);
            event->type = fmidi_event_message;
            event->delta = ev_delta;
            event->datalen = midi_size;
            memcpy(event->data, midi, midi_size);
            ev_delta = 0;
        }

        ev_delta += delta_inc;
    }

    fmidi_event_t *event = fmidi_event_alloc(evbuf, 1);
    event->type = fmidi_event_meta;
    event->delta = ev_delta;
    event->datalen = 1;
    event->data[0] = 0x2f;

    uint32_t evdatalen = track.length = evbuf.size();
    uint8_t *evdata = new uint8_t[evdatalen];
    track.data.reset(evdata);
    memcpy(evdata, evbuf.data(), evdatalen);

    return smf.release();
}

fmidi_smf_t *fmidi_mus_file_read(const char *filename)
{
    unique_FILE fh(fmidi_fopen(filename, "rb"));
    if (!fh)
        RET_FAIL(nullptr, fmidi_err_input);

    fmidi_smf_t *smf = fmidi_mus_stream_read(fh.get());
    return smf;
}

fmidi_smf_t *fmidi_mus_stream_read(FILE *stream)
{
    rewind(stream);

    constexpr size_t mus_file_size_limit = 65536;
    uint8_t buf[mus_file_size_limit];

    size_t length = fread(buf, 1, mus_file_size_limit, stream);
    if (ferror(stream))
        RET_FAIL(nullptr, fmidi_err_input);

    fmidi_smf_t *smf = fmidi_mus_mem_read(buf, length);
    return smf;
}
