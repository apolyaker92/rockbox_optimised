# Rockbox optimised (iPod Video)

Personal Rockbox build for an iPod Video (5th gen) with a 1 TB iFlash card and a large library: speed fixes for big playlists and big folder trees, plus two features.

## Which branch to use

| Branch | What it is |
|---|---|
| **`stable-4.0`** | **Use this one.** Rockbox 4.0 (the stable release) plus the changes below. |
| `master` | The same fixes on the Rockbox development version of 25 Sep 2026. Not recommended: that development version had USB and disk problems of its own. |
| `perf-logging` | `master` plus timing logs used while measuring. For testing only. |
| `my-rockbox-config` | Backup of the iPod's `.rockbox` folder (themes, fonts, settings). |

## Changes in `stable-4.0`

Performance and fixes:

- **Inserting into a large playlist** (Insert, Insert next, Insert shuffled, Insert last while shuffled) no longer shifts the whole playlist for every track.
- **Playing or inserting a folder tree** no longer re-reads and re-sorts each parent folder after every subfolder.
- **Resuming a large playlist** at power-on batches the replayed inserts the same way.
- **Turning shuffle off** keeps the directory cache references instead of making the iPod find every track again, without allocating memory (an earlier version froze the iPod doing that).
- **VU meter on the playing screen** only sends the meter area to the display each frame, and slow frames no longer pile up.
- **Corrupt resume files** can no longer make the playlist code write outside its buffer.

Features:

- **Jump to first letter** in the file browser: hold Select and choose "By First Letter...", or set it as the File View hotkey (Select+Play). One script at a time: Latin, then Cyrillic, Japanese, Korean with Menu.
- **Listening Stats** plugin: top tracks, albums and artists, recently played, a summary, and a "Most Played" playlist, from the playback log.

Tools:

- `tools/fnt_subset.py` trims a Rockbox `.fnt` font to a character range (useful for fonts that only ever show ASCII).
- `tools/build-ipod.sh` builds the toolchain, the firmware and the simulator.

All playlist changes were checked in the simulator against unmodified Rockbox 4.0 and produce identical playlists and resume files. The directory cache parts only run on the iPod, see `TESTING.md`.

## Building

```
tools/build-ipod.sh deps        # build dependencies (Debian/Ubuntu, as root)
tools/build-ipod.sh toolchain   # ARM cross compiler, about 30 minutes, once
tools/build-ipod.sh firmware    # -> ~/rockbox-build/ipod/rockbox.zip
tools/build-ipod.sh sim         # desktop simulator
```

## Installing

1. Back up the `.rockbox` folder on the iPod.
2. Unzip `rockbox.zip` onto the iPod's root, replacing files. Themes, fonts and `config.cfg` are not in the zip, so they are kept.
3. Eject, let it reboot, and check the version in System > Rockbox Info.

To go back, restore the `.rockbox` backup (or unzip the official Rockbox 4.0 build).

## Testing

See `TESTING.md`. After a test session, run `chkdsk` on the iPod in disk mode so file system problems show up early.
