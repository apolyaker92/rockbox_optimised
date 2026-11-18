/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Synced lyrics (.lrc) for the %Ly skin tag.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#ifndef _SKIN_LYRICS_H_
#define _SKIN_LYRICS_H_

#include <stdbool.h>

struct mp3entry;

/* Returns the lyric line that is `rel` lines away from the one that should
 * be showing at `elapsed_ms` (0 = current, -1 = previous, 1 = next) for the
 * track `id3`, read from the .lrc file next to the audio file. Returns NULL
 * when there are no lyrics, the line is out of range, or the line is blank.
 * The returned string stays valid until a different track's lyrics load. */
const char *skin_lyrics_line(const struct mp3entry *id3,
                             unsigned long elapsed_ms, int rel);

/* The line showing at elapsed_ms (NULL during a gap or before the first
 * line), how far through it we are in 0..1000 (a guess from the time to the
 * next line, capped for lines followed by a long pause), and the line after
 * it (NULL if none or blank). Returns false when there are no lyrics. */
bool skin_lyrics_now(const struct mp3entry *id3, unsigned long elapsed_ms,
                     const char **cur, int *permille, const char **next);

/* What the lyric area shows: decided by the track (auto), or forced. */
enum lyric_mode { LYRIC_MODE_AUTO = 0, LYRIC_MODE_LYRICS, LYRIC_MODE_BANDS };
enum lyric_mode skin_lyrics_cycle_mode(void);
const char *skin_lyrics_mode_text(void);     /* "AUTO", "LYR" or "BARS" */

/* For the %Lh switch: lyrics in the area (true) or the bars (false). */
bool skin_lyrics_wanted(const struct mp3entry *id3);

/* True when the track has a .lrc with at least one time stamped line. */
bool skin_lyrics_available(const struct mp3entry *id3);

#endif /* _SKIN_LYRICS_H_ */
