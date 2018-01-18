# fmidi
Fmidi is a library to read and play back MIDI files. It supports both standard MIDI files and RIFF MIDI files.

The functionality is exposed as a C programming interface, and it is implemented with C++.

It is a simple library which is good for implementing a MIDI file player, or any program taking MIDI files as inputs.
In fact, a player with a terminal interface is provided as an [example](https://github.com/jpcima/fmidi/blob/master/programs/midi-play.cc). Build it with [ncurses](https://www.gnu.org/software/ncurses/), [libev](https://github.com/enki/libev) and [RtMidi](https://github.com/thestk/rtmidi).

## Design goals

This library is designed for excellent compatibility. Downloaded MIDI files often come as corrupted, incomplete, or non-standard in many ways. Fmidi has a permissive reader, which attempts repairs when errors are detected, without compromise on the MIDI file standard.
