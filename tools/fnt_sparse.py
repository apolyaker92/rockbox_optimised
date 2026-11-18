#!/usr/bin/env python3
"""Keep only the glyphs a library needs in a Rockbox .fnt.

usage: fnt_sparse.py CHARS.json IN.fnt OUT.fnt [--wide]

CHARS.json comes from scan_chars.py. Characters that are kept: ASCII,
Latin-1 and Latin Extended-A, general punctuation, arrows, all kana, CJK
symbols and full-width forms, plus everything in the library. --wide also
keeps the Japanese standard set (JIS X 0208, all common kanji), the 2,350
common Hangul syllables (KS X 1001) and all Greek and Cyrillic, so music
added later still shows.

Everything else points at the font's default glyph, so the file shrinks a
lot (a 1.8 MB LanaPixel becomes about 0.9 MB with --wide, 0.4 MB without) and
stock Rockbox loads it as a normal font. Uses fnt_subset.py for the format.
"""
import json
import struct
import sys

import fnt_subset as fs

SAFE = [(0x20, 0x7E), (0xA0, 0x17F), (0x2000, 0x206F), (0x2190, 0x21FF),
        (0x3000, 0x30FF), (0xFF00, 0xFFEF)]


def codec_chars(codec, leads):
    """Every character a two-byte legacy codec can decode."""
    found = set()
    for lead in leads:
        for trail in range(0x40, 0xFF):
            try:
                text = bytes([lead, trail]).decode(codec)
            except UnicodeDecodeError:
                continue
            if len(text) == 1:
                found.add(ord(text))
    return found


def wide_set():
    chars = {c for c in codec_chars("shift_jis", range(0x81, 0xEF))
             if c >= 0x2E80 or 0x370 <= c <= 0x52F}
    chars |= {c for c in codec_chars("euc_kr", range(0xB0, 0xC9))
              if 0xAC00 <= c <= 0xD7A3}
    chars |= set(range(0x370, 0x530))               # Greek and Cyrillic
    return chars


def main(chars_path, src, dst, wide):
    keep = {int(k) for k in json.load(open(chars_path))}
    for lo, hi in SAFE:
        keep.update(range(lo, hi + 1))
    if wide:
        keep |= wide_set()
    f = fs.load(src)
    if not f["offsets"]:
        sys.exit(f"{src}: fixed-width font, not supported")
    first, size = f["firstchar"], f["size"]
    last = first + size - 1
    default = f["defaultchar"]
    di = default - first
    doff, dwid = f["offsets"][di], f["widths"][di]
    have = [c for c in keep if first <= c <= last] + [default]
    lo, hi = min(have), max(have)
    n = hi - lo + 1
    ends = fs.glyph_ends(f)
    bits, remap = bytearray(), {}

    def place(off):
        if off not in remap:
            remap[off] = len(bits)
            bits.extend(f["bits"][off:ends[off]])
        return remap[off]

    offsets, widths = [], []
    for c in range(lo, hi + 1):
        i = c - first
        if c in keep and 0 <= i < size:
            offsets.append(place(f["offsets"][i]))
            widths.append(f["widths"][i])
        else:
            offsets.append(place(doff))
            widths.append(dwid)
    out = bytearray(fs.HEADER.pack(b"RB12", f["maxwidth"], f["height"],
                                   f["ascent"], f["depth"], lo, default, n,
                                   len(bits), n, n))
    out += bits
    wide_offsets = len(bits) >= fs.OFFSET16_LIMIT
    while len(out) % (4 if wide_offsets else 2):
        out.append(0)
    out += struct.pack("<%d%s" % (n, "I" if wide_offsets else "H"), *offsets)
    out += bytes(widths)
    open(dst, "wb").write(out)
    print(f"{src}: {f['filesize']} -> {len(out)} bytes, "
          f"U+{lo:04X}..U+{hi:04X}, {len(set(offsets))} glyph bitmaps")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--wide"]
    if len(args) != 3:
        print(__doc__)
        sys.exit(1)
    main(*args, wide="--wide" in sys.argv)
