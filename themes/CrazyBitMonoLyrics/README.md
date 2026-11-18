# CrazyBitMonoLyrics

A playing screen for an iPod Video (320x240) that uses the `%Lk`, `%Lh`,
`%Lm` and `%Lz` tags from this branch: lyrics with a grey sweep for what is
coming up, or eight band bars for tracks without lyrics, with icons for the
artist, title and album, a small volume readout in the header, and quiet
shuffle/repeat flags. Based on CrazyBitMono (CC-BY-SA, see the credits at the
top of the `.wps`).

It needs the firmware from this branch: with stock Rockbox the `%L` tags are
unknown and the theme shows an error.

## Install

1. Build and install the firmware (see the main README).
2. Copy `wps/*` into `.rockbox/wps/` and `themes/*` into `.rockbox/themes/`.
3. Put the fonts it uses in `.rockbox/fonts/` (below).
4. Settings > Theme Settings > Browse Theme Files > `CrazyBitMonoLyrics.cfg`.

## Fonts

The theme uses `14-LanaPixel`, `07-LanaPixel`, `10-ProFont`, `20-digital7mono`
and, for menus, `wqy12`. The LanaPixel and wqy12 fonts hold tens of thousands
of CJK glyphs and Rockbox reads big fonts from disk as it draws, so the theme
points at trimmed copies named `14-LanaPixel-trim.fnt`, `07-LanaPixel-trim.fnt`
and `wqy12-trim.fnt`. Make them from your own library:

    python tools/scan_chars.py /path/to/Music chars.json
    python tools/fnt_sparse.py chars.json 14-LanaPixel.fnt 14-LanaPixel-trim.fnt --wide

and the same for the other two fonts. `--wide` also keeps all common kanji,
Hangul and Cyrillic so music added later still shows. Or skip all of this and
edit the `%Fl` font lines in the `.wps` and `.sbs` and the `font:` line in the
`.cfg` to point at the untrimmed fonts.

## Keys

Select+Left cycles lyrics / bands / automatic, Select+Right holds the
backlight on.
