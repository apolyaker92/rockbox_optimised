#!/usr/bin/env python3
"""Trim a Rockbox .fnt font to a maximum character code.

Rockbox fonts store one contiguous range of code points, so dropping
everything above a limit (for example CJK, which starts at U+2E80) is a
matter of cutting the tables and the bitmap data they reference.

usage: fnt_subset.py info FONT...
       fnt_subset.py trim LIMIT IN.fnt OUT.fnt    (LIMIT like 0x2E7F)
"""
import struct
import sys

HEADER = struct.Struct("<4sHHHHIIIIII")
OFFSET16_LIMIT = 0xFFDB


def load(path):
    data = open(path, "rb").read()
    (magic, maxwidth, height, ascent, depth, firstchar, defaultchar, size,
     bits_size, noffset, nwidth) = HEADER.unpack_from(data)
    if magic != b"RB12":
        raise SystemExit(f"{path}: not an RB12 font")
    pos = HEADER.size
    bits = data[pos:pos + bits_size]
    pos += bits_size
    wide = bits_size >= OFFSET16_LIMIT
    align = 4 if wide else 2
    pos = (pos + align - 1) // align * align
    offsets = []
    if noffset:
        fmt = "<%d%s" % (noffset, "I" if wide else "H")
        offsets = list(struct.unpack_from(fmt, data, pos))
        pos += struct.calcsize(fmt)
    widths = list(data[pos:pos + nwidth]) if nwidth else []
    return dict(maxwidth=maxwidth, height=height, ascent=ascent, depth=depth,
                firstchar=firstchar, defaultchar=defaultchar, size=size,
                bits=bits, offsets=offsets, widths=widths, filesize=len(data))


def glyph_ends(font):
    """Map each glyph offset to the end of its bitmap data."""
    starts = sorted(set(font["offsets"])) + [len(font["bits"])]
    return {s: e for s, e in zip(starts, starts[1:])}


def info(path):
    f = load(path)
    last = f["firstchar"] + f["size"] - 1
    print(f"{path}: {f['filesize']} bytes, height {f['height']}, depth "
          f"{f['depth']}, U+{f['firstchar']:04X}..U+{last:04X}, "
          f"default U+{f['defaultchar']:04X}")
    if not f["offsets"]:
        return
    ends = glyph_ends(f)
    default = f["offsets"][f["defaultchar"] - f["firstchar"]]
    blocks = [(0x0000, "Latin/Greek/Cyrillic/symbols (< U+2E80)"),
              (0x2E80, "CJK, kana, Hangul and other East Asian"),
              (0xE000, "private use and compatibility (>= U+E000)")]
    for i, (start, name) in enumerate(blocks):
        end = blocks[i + 1][0] if i + 1 < len(blocks) else 0x110000
        used = set()
        for c in range(max(start, f["firstchar"]), min(end, last + 1)):
            off = f["offsets"][c - f["firstchar"]]
            if off != default:
                used.add(off)
        nbytes = sum(ends[o] - o for o in used)
        print(f"    {name}: {len(used)} glyphs, {nbytes} bytes of bitmaps")


def trim(limit, src, dst):
    f = load(src)
    last = min(f["firstchar"] + f["size"] - 1, limit)
    if f["defaultchar"] > last:
        raise SystemExit("default character would be cut off")
    n = last - f["firstchar"] + 1
    offsets = f["offsets"][:n]
    widths = f["widths"][:n]

    # repack only the bitmaps still referenced, keeping shared glyphs shared
    ends = glyph_ends(f) if offsets else {}
    newbits = bytearray()
    remap = {}
    for off in offsets:
        if off not in remap:
            remap[off] = len(newbits)
            newbits += f["bits"][off:ends[off]]
    if not offsets:  # fixed width font: glyphs are laid out in order
        per = len(f["bits"]) // f["size"]
        newbits = f["bits"][:per * n]
    offsets = [remap[o] for o in offsets]

    wide = len(newbits) >= OFFSET16_LIMIT
    out = bytearray(HEADER.pack(b"RB12", f["maxwidth"], f["height"],
                                f["ascent"], f["depth"], f["firstchar"],
                                f["defaultchar"], n, len(newbits),
                                len(offsets), len(widths)))
    out += newbits
    align = 4 if wide else 2
    while len(out) % align:
        out.append(0)
    if offsets:
        out += struct.pack("<%d%s" % (len(offsets), "I" if wide else "H"),
                           *offsets)
    out += bytes(widths)
    open(dst, "wb").write(out)
    print(f"{src}: {f['filesize']} -> {len(out)} bytes "
          f"(U+{f['firstchar']:04X}..U+{last:04X})")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "info":
        for p in sys.argv[2:]:
            info(p)
    elif len(sys.argv) == 5 and sys.argv[1] == "trim":
        trim(int(sys.argv[2], 0), sys.argv[3], sys.argv[4])
    else:
        print(__doc__)
        sys.exit(1)
