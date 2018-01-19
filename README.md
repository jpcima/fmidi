# fmidi
Fmidi is a library to read and play back MIDI files. It supports both standard MIDI files and RIFF MIDI files.

The functionality is exposed as a C programming interface, and it is implemented with C++.

It is a simple library which is good for implementing a MIDI file player, or any program taking MIDI files as inputs.
In fact, a player with a terminal interface is provided as an [example](https://github.com/jpcima/fmidi/blob/master/programs/midi-play.cc). Build it with [ncurses](https://www.gnu.org/software/ncurses/), [libev](https://github.com/enki/libev) and [RtMidi](https://github.com/thestk/rtmidi).

## Design goals

This library is designed for excellent compatibility. Downloaded MIDI files often come as corrupted, incomplete, or non-standard in many ways. Fmidi has a permissive reader, which attempts repairs when errors are detected, without compromise on the MIDI file standard.

## Quality comparison

Fmidi is tested on a collection of MIDI files and the results are displayed in the table below. The data gives an idea of the relative compatibility and robustness of the library.

Tests are performed under different libraries with AddressSanitizer and identical environments. Under each error situation, the data indicates the number of problematic files.

<table>
  <tr><th>Library</th><th>Reading error</th><th>AddressSanitizer issue</th><th>LeakSanitizer issue</th></tr>
  <tr><td>Fmidi</td><td>20</td><td>0</td><td>0</td></tr>
  <tr><td>libsmf 1.3</td><td>468</td><td>265</td><td>108</td></tr>
  <tr><td>portsmf 228</td><td>979</td><td>7</td><td>0</td></tr>
  <tr><td>libtimidity 0.2.5</td><td>493</td><td>0</td><td>0</td></tr>
  <tr><th>Total files</th><td colspan="3">86808</td></tr>
</table>
