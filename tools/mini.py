#!/usr/bin/env python
# -*- coding: utf-8; -*-

# Roll up the entire library implementation into a single cpp file.
# This allows the library to be amalgated cleanly as a pair of source files.
# `fmidi/fmidi.h` `fmidi/fmidi.cpp`

import os
import sys
import fnmatch
import subprocess
import re

source_dir = 'sources'
processed = set()
file_blacklist = [os.path.join(source_dir, "fmidi/fmidi.h")]
re_incl = re.compile(r'\s*#\s*include\s*"([^\"]+)"')
re_once = re.compile(r'\s*#\s*pragma\s+once\b')
out = sys.stdout

def process_file(file):
    if file in processed:
        return

    processed.add(file)

    lines = open(file).readlines()
    line_count = len(lines)
    line_no = 0

    # skip the copyright header
    while line_no < line_count and lines[line_no].startswith('//'):
        line_no += 1

    while line_no < line_count:
        line = lines[line_no]
        if re_once.match(line):
            line_no += 1
            continue
        match_incl = re_incl.match(line)
        if not match_incl:
            out.write(line)
        else:
            incl_name = match_incl.group(1)
            candidates = [
                os.path.join(os.path.dirname(file), incl_name),
                os.path.join(source_dir, incl_name),
            ]
            selected = next(filter(os.path.exists, candidates), None)
            if selected is None or selected in file_blacklist:
                out.write(line)
            else:
                process_file(selected)
        line_no += 1

def search_files_orderly(source_dir, pattern):
    items = []
    for root, dirs, files in os.walk(source_dir):
        for file in files:
            if fnmatch.fnmatch(file, pattern):
                items.append((root, file))
    for root, file in sorted(items):
        yield os.path.join(root, file)

if __name__ == '__main__':
    if not os.path.isdir(source_dir):
        sys.stderr.write('Please run this tool in the project directory.\n')
        sys.exit(1)

    revision = '(unknown)'
    try:
        proc = subprocess.Popen(['git', 'rev-parse', '--short', 'HEAD'], stdout=subprocess.PIPE)
        revision = proc.communicate()[0].decode('utf-8').strip()
    except:
        pass

    out.write(
"""// =============================================================================
//
// The Fmidi library - a free software toolkit for MIDI file processing
// Single-file implementation, based on software revision: %s
//
// =============================================================================
//          Copyright Jean Pierre Cimalando 2018-2022.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
// =============================================================================
""" % (revision))

    for path in search_files_orderly(source_dir, '*.c*'):
        process_file(path)
