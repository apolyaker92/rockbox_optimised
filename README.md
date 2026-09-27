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
- **Listening Stats** plugin (Plugins > Applications > listen_stats): top tracks, albums and artists, recently played, a summary, and a "Most Played" playlist, all from Rockbox's playback log. Artist and album come from folder names, so it works without the database.

## Is it safe?

Every playlist change was checked in the Rockbox simulator against unmodified 4.0: the same inserts, shuffles and resumes, with the full playlist and resume file compared after each step. Both builds produced identical output, including a playlist at the 32,000-track limit, so resume files carry over in both directions and you can switch between this build and stock.

The directory cache parts only run on real hardware. They're in daily use on my iPod, but back up your `.rockbox` folder first. [TESTING.md](TESTING.md) has the checks I used.

## Installing

Needs an iPod Video that already runs Rockbox 4.0 (install it with [Rockbox Utility](https://www.rockbox.org/wiki/RockboxUtility) first).

1. Build `rockbox.zip` (below).
2. Back up the `.rockbox` folder on the iPod.
3. Unzip `rockbox.zip` onto the iPod's root, replacing files. Themes, fonts and `config.cfg` aren't in the zip, so yours are kept.
4. Eject, let it reboot, and check System > Rockbox Info.

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
