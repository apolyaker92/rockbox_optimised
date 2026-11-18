/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Cheap band visualizer for the %Lz skin tag.
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
#ifndef _SKIN_VIS_H_
#define _SKIN_VIS_H_

#include <stdbool.h>
#include <stdint.h>

#define SKIN_VIS_BANDS 8
#define SKIN_VIS_FPS   10

/* Level 0..100 of band 0 (bass) .. SKIN_VIS_BANDS-1 (treble). Looks at the
 * audio that is about to play, at most SKIN_VIS_FPS times a second, and
 * smooths the result (fast rise, slow fall). */
int skin_vis_band(int band);

/* True while a bar has been drawn in the last second, so the WPS can wake
 * up SKIN_VIS_FPS times a second instead of its usual rate. */
bool skin_vis_active(void);

/* Same, for other things that animate (the lyric box). */
void skin_vis_mark_active(void);

/* Analysis core, separate from the PCM plumbing so it can be tested on a
 * host: levels[] gets 0..100 per band from `frames` interleaved stereo
 * 16-bit frames at `rate` Hz. `state` carries the smoothing between calls. */
void skin_vis_analyze(const int16_t *pcm, int frames, unsigned rate,
                      int levels[SKIN_VIS_BANDS]);

#endif /* _SKIN_VIS_H_ */
