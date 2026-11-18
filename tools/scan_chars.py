#!/usr/bin/env python3
"""List the characters a music library needs, for fnt_sparse.py.

usage: scan_chars.py MUSIC_ROOT OUT.json

Walks MUSIC_ROOT and counts every character in folder names, file names and
the title / artist / album / album artist / genre / composer tags (read with
mutagen: pip install mutagen). Run it on a copy of the library on your
computer; over USB it takes a long time.
"""
import collections
import json
import os
import sys

try:
    import mutagen
except ImportError:
    sys.exit("needs mutagen: pip install mutagen")

AUDIO = {".flac", ".mp3", ".m4a", ".ogg", ".opus", ".wav", ".wv", ".ape",
         ".mpc", ".aac", ".wma"}
TAGS = ("title", "artist", "album", "albumartist", "genre", "composer",
        "performer", "date")


def main(root, out):
    counts = collections.Counter()
    files = errors = 0

    def add(text):
        for ch in text:
            counts[ord(ch)] += 1

    for folder, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs
                   if d not in ("System Volume Information", ".rockbox")]
        add(os.path.basename(folder))
        for name in names:
            add(name)
            if os.path.splitext(name)[1].lower() not in AUDIO:
                continue
            files += 1
            if files % 1000 == 0:
                print(f"{files} files...", file=sys.stderr)
            try:
                tags = mutagen.File(os.path.join(folder, name), easy=True)
                for key in TAGS:
                    for value in (tags.get(key, []) if tags else []):
                        add(str(value))
            except Exception:
                errors += 1
    json.dump(counts, open(out, "w"))
    print(f"{files} audio files, {errors} unreadable tags, "
          f"{len(counts)} distinct characters")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    main(*sys.argv[1:])
