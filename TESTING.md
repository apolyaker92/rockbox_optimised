# Testing this build on an iPod Video (5th gen)

This branch is Rockbox 4.0 (the stable release) plus the changes below. See README.md for an overview.

The performance changes speed up:

1. Inserting tracks into a large playlist (Insert, Insert next, Insert last while shuffled, Insert shuffled)
2. Playing or inserting a folder that has lots of subfolders (with recursive insert enabled)
3. Resuming a large playlist at power-on
4. Turning shuffle off (the directory cache no longer has to find every track again)
5. The VU meter on the playing screen (only the meter area is sent to the display, and slow frames no longer pile up)

The simulator can't exercise the directory cache, so this device test is the only check of that part.

## Before you start

- Copy the whole `.rockbox` folder from the iPod to your computer. To go back, delete `.rockbox` on the iPod and copy the backup over it.
- Check these settings and keep them the same for both builds:
  - Max playlist size (the default on the iPod Video is only 2000 tracks)
  - Recursively insert directories: Yes
  - Directory cache: On
- Use a stopwatch and do each test 3 times. Write down the median.

## Timings (stock build first, then this build)

| # | Test | How to time it |
|---|------|----------------|
| A | Insert next into a big playlist | Play a playlist of at least 10,000 tracks. In the file browser, open the context menu on a folder with several hundred tracks and choose "Insert next". Time from pressing select until the "Inserted N tracks" message is gone. |
| B | Play a big folder tree | In the file browser, highlight a folder with many subfolders (for example your top-level Music folder) and press Play. Time until music starts. |
| C | Resume at power-on | After test A, power off by holding Play. Power on and time from the Rockbox logo until music resumes. |
| D | Shuffle off | While playing a big playlist with shuffle on, turn shuffle off. It must not freeze (an earlier version on the development code did). Note how long the disk / CPU stays busy afterwards. |
| E | VU meter | On the playing screen with music playing, change the volume up and down for 10 seconds. Note how responsive it feels compared to stock. |

## New features

| # | Feature | How to test it |
|---|---------|----------------|
| F | Jump to first letter | In the file browser hold Select on any entry and choose "By First Letter..." (or set Settings > General Settings > File View > File Browser Hotkey (last item) to "By First Letter..." and press Select+Play). The list starts with Latin letters (and digits) found in the folder; Menu switches to Cyrillic, Japanese, Korean. Pick a letter and check the browser jumps to the first entry with it. Left closes it without jumping. |
| G | Listening Stats | Plugins > Applications > listen_stats. The first time it offers to turn on playback logging. Play some tracks all the way (or at least half), then open it again: Top Tracks/Albums/Artists, Recently Played and Summary should match what you played. "Save Most Played Playlist" writes /Playlists/Most Played.m3u8 and can start it. |

## Check that nothing broke

After each test:

- Open the playlist viewer. Check that the order looks right (inserted tracks in the right place, queued tracks marked).
- Skip forward and back a few tracks. Check that the track that plays matches the name shown.
- After test D: skip through a few tracks and use the playlist viewer. Wrong names or wrong songs would point to the directory cache part.
- After test C: check that playback resumes on the same track and position.
- After test E: the VU meters and the rest of the playing screen should look exactly as before (no stale areas or flicker).

If anything looks wrong, restore the `.rockbox` backup and note which test failed.

After the test session, run `chkdsk X:` (read-only, X = the iPod's drive letter) in disk mode, so any file system problem shows up after one session instead of five.
