# Rockbox for big libraries (iPod Video)

[Rockbox](https://www.rockbox.org) 4.0 with fixes for a large music library on an iPod Video (5th gen), plus two features. Built and tested on my own iPod: a 1 TB iFlash card, a big folder-based library, large playlists, and no database.

The write-up, with what was slow, why, and what went wrong along the way: [Making Rockbox fast on a 1 TB iPod](https://andrewpolyakov.com/blog/speeding-up-rockbox).

This is a personal fork, not an official Rockbox build.

## What's different

Speed:

- **Inserting into a large playlist** (Insert, Insert next, Insert shuffled, Insert last while shuffled) no longer shifts the whole playlist for every track added.
- **Playing or inserting a folder tree** no longer re-reads and re-sorts each parent folder after every subfolder.
- **Resuming a large playlist** at power-on batches the replayed inserts the same way.
- **Turning shuffle off** keeps each track's directory cache entry instead of making the iPod find every track again.
- **The VU meter on the playing screen** only sends the meter to the display each frame, and slow frames no longer pile up.

Measured in the simulator against stock 4.0:

| | Stock 4.0 | This build |
|---|---|---|
| Insert 4,000 tracks into 24,000 | 0.43 s | 0.26 s |
| Play a folder of 3,000 artist folders | 10.7 s | 0.66 s |
| Resume 16,000 tracks + 4,000 inserted | 0.08 s | ~0 s |

The iPod's CPU is far slower than a desktop's, so the gaps are bigger on the device.

Fixes:

- A corrupt resume file can no longer make the playlist code write outside its buffer.

Features:

- **Jump to first letter** in the file browser. Hold Select on any entry and choose "By First Letter...", or set Settings > General Settings > File View > File Browser Hotkey (the last item) to it and press Select+Play. It lists the first letters that exist in the folder, Latin first; Menu switches to Cyrillic, Japanese and Korean.
- **Listening Stats** in the main menu: top tracks, albums and artists, recently played, a summary, and a "Most Played" playlist, all from Rockbox's playback log, for all time or just the last 12 months, 3 months, month or week (Period, at the top of the menu, on players with a clock). Artist and album come from folder names, so it works without the database: normally the two folders above a track (`/Music/Artist/Album`). If your library is laid out differently, mark folders in Library Folders (in the stats menu): an artists folder holds artist folders, like `/Music/Albums`, and an albums folder holds albums that each count as their own artist, like `/Music/OST`. Disc folders ("Disc 1", "CD2") count as their album. Select on a track, album or artist opens it in the file browser. Old playback logs are merged into a summary (`.rockbox/listen_stats.dat`, with the logs kept in `.rockbox/playback_archive/`), so it opens quickly however long the history gets, and it has room for over a million tracks. Playback stops while the stats are open, and "Most Played" is saved to the playlist catalog folder (`/Playlists` unless set otherwise). It's also in Plugins > Applications as listen_stats.
- **Recently Added** in the main menu: the albums and soundtracks added in the last two weeks, newest first, as "Album - Artist", from the dates of their folders (copying keeps a file's date but makes its folders new). It lists the folders inside the ones marked in Library Folders (shared with Listening Stats, and on the context menu here), so the albums in each artist's folder in an artists folder and each folder in an albums folder; with nothing marked, the start directory or `/Music` counts as an artists folder. Select opens the album in the file browser. Playback keeps going, and it's also in Plugins > Applications as recently_added.

## Lyrics, band visualizer and shortcuts on the playing screen

Skin tags for the playing screen. Nothing runs unless a theme uses them. A
theme that does is in [themes/CrazyBitMonoLyrics](themes/CrazyBitMonoLyrics).

| Tag | What it does |
|---|---|
| `%Lk(x,y,w,h)` | **Lyric box.** The current line word-wrapped (it never scrolls): the part already sung in the viewport colour, the rest grey, then the next line grey underneath. Says "Lyrics not found" when there is nothing to show. |
| `%Ly` / `%Ly(n)` | The lyric line `n` away from the current one (default 0), as plain text. |
| `%Lh` | True when the lyric area should show lyrics, for `%?Lh<lyrics\|bands>`. Follows the mode below. |
| `%Lm` | `AUTO`, `LYR` or `BARS`, the current mode. |
| `%Lz(x,y,w,h,vertical,noborder,bandN)` | One of eight band bars, `band0` (bass) to `band7` (treble). |

**Lyrics** come from a `.lrc` with the same name as the audio file
(`song.flac` and `song.lrc`). Lines with several time stamps, `[offset:]`, a
byte order mark and word stamps like `<00:12.34>` (stripped) are handled.
**Lines without a `[mm:ss.xx]` stamp are ignored.** A file with no stamps at
all has nothing to show: automatic mode shows the bands instead, and lyrics
only mode shows "Lyrics not found". At most 12 KB and 512 lines of a file are
read, once when the track changes, so drawing a line costs a binary search.
A plain `.lrc` only has when each line starts, so the grey-to-white sweep
inside a line is an estimate spread over the line's duration (a line followed
by a long pause is capped at about 90 ms a character).

**Bands** are eight Goertzel filters, not an FFT, run ten times a second over a
rolling copy of the last 4096 frames the mixer played (about 16 KB of RAM).
The WPS wakes up ten times a second only while the lyric box or the bars are
on screen, and without the peak meter's busy loop, which is why this is much
lighter than the peak meters on an iPod Video.

**Keys (iPod):** Select+Left cycles automatic / lyrics only / bands only,
Select+Right holds the backlight on (press again to restore your timeouts),
Select+Play is still the WPS hotkey. Other targets have the actions
(`ACTION_WPS_LYRIC_MODE`, `ACTION_WPS_BACKLIGHT_HOLD`) but no keys for them.

Tested on an iPod Video (5th gen) only.

### Getting the fonts and lyrics

- `tools/scan_chars.py MUSIC_ROOT chars.json` lists every character a library
  uses, and `tools/fnt_sparse.py chars.json IN.fnt OUT.fnt [--wide]` keeps only
  those glyphs in a font, so the theme's CJK fonts shrink from about 20 MB to
  about 2 MB. See the theme's README.

## Is it safe?

Every playlist change was checked in the Rockbox simulator against unmodified 4.0: the same inserts, shuffles and resumes, with the full playlist and resume file compared after each step. Both builds produced identical output, including a playlist at the 32,000-track limit, so resume files carry over in both directions and you can switch between this build and stock.

The directory cache parts only run on real hardware. They're in daily use on my iPod, but back up your `.rockbox` folder first. [TESTING.md](TESTING.md) has the checks I used.

## Installing

Needs an iPod Video that already runs Rockbox 4.0 (install it with [Rockbox Utility](https://www.rockbox.org/wiki/RockboxUtility) first).

1. Build `rockbox.zip` (below).
2. Back up the `.rockbox` folder on the iPod.
3. Unzip `rockbox.zip` onto the iPod's root, replacing files. `config.cfg` isn't in the zip, and neither are your own themes and fonts, so yours are kept; only the stock cabbiev2 theme and default font are replaced.
4. Eject, let it reboot, and check System > Rockbox Info.

If you've changed the order of the main menu, Listening Stats won't be in it yet. Add it with Plugins > Applications > main_menu_config.

To go back, restore your backup or unzip the official 4.0 build.

## Building

On Debian or Ubuntu:

```
tools/build-ipod.sh deps        # build dependencies (as root)
tools/build-ipod.sh toolchain   # ARM cross compiler, about 30 minutes, once
tools/build-ipod.sh firmware    # -> ~/rockbox-build/ipod/rockbox.zip
tools/build-ipod.sh sim         # desktop simulator
```

## Tools

- `tools/fnt_subset.py` trims a Rockbox `.fnt` font to a character range, for fonts that only ever show ASCII (a battery percentage, a volume readout). Fonts with CJK glyphs can be several megabytes, and Rockbox reads them from disk as it draws.
- `tools/build-ipod.sh` builds the toolchain, the firmware and the simulator.

## License

Rockbox is GPL version 2 or later, and so are these changes. See [docs/COPYING](docs/COPYING).
