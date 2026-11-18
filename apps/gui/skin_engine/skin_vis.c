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
 * No FFT: eight Goertzel filters, one per band, each run over a short
 * window of the audio that is about to play. Bass bands run on a signal
 * averaged down by 8 and mid bands on one averaged down by 2, so each
 * filter only needs 128 points to separate its band. About 1500 multiplies
 * per frame, ten frames a second.
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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "skin_vis.h"

#define N_PTS 128           /* points per filter */

/* Band centre frequencies in Hz and which signal each one runs on:
 * decimation 8 for the bass bands, 2 for the mids, 1 for the highs. */
static const struct {
    uint16_t hz;
    uint8_t dec;
} bands[SKIN_VIS_BANDS] = {
    {  60, 8 }, { 140, 8 }, { 320, 8 }, { 700, 8 },
    { 1500, 2 }, { 3200, 2 },
    { 6500, 1 }, { 12000, 1 },
};

/* cos(2*pi*hz/fs) in Q14, from a Taylor series in Q28 (no float, no table).
 * Only called when the sample rate changes. */
static int32_t cos_q14(unsigned hz, unsigned fs)
{
    /* x = 2*pi*hz/fs in Q28, kept in [0, pi] */
    int64_t x = (int64_t)hz * 1686629713LL / (int64_t)fs;
    int neg = 0;
    const int64_t half_pi = 421657428LL;           /* pi/2 in Q28 */
    int64_t x2, x4, x6, x8, c;

    if (x > 2 * half_pi)
        x = 2 * half_pi;
    if (x > half_pi)
    {
        x = 2 * half_pi - x;
        neg = 1;
    }
    x2 = (x * x) >> 28;
    x4 = (x2 * x2) >> 28;
    x6 = (x4 * x2) >> 28;
    x8 = (x4 * x4) >> 28;
    c = (1LL << 28) - x2 / 2 + x4 / 24 - x6 / 720 + x8 / 40320;
    c >>= 14;                                      /* Q28 -> Q14 */
    return (int32_t)(neg ? -c : c);
}

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > v)
        bit >>= 2;
    while (bit)
    {
        if (v >= r + bit)
        {
            v -= r + bit;
            r = (r >> 1) + bit;
        }
        else
            r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

/* log2(a) in Q8, linear between powers of two (about 0.5 dB error) */
static int log2_q8(uint32_t a)
{
    int msb = 31;
    uint32_t mant;
    if (a == 0)
        return 0;
    while (!(a >> msb))
        msb--;
    mant = msb >= 8 ? (a >> (msb - 8)) & 0xFF : (a << (8 - msb)) & 0xFF;
    return (msb << 8) + (int)mant;
}

/* Goertzel power of x[0..N_PTS) at the filter with coefficient coef (Q14) */
static uint64_t goertzel(const int32_t *x, int32_t coef)
{
    int64_t s1 = 0, s2 = 0, p;
    int i;
    for (i = 0; i < N_PTS; i++)
    {
        int64_t s = x[i] + ((coef * s1) >> 14) - s2;
        s2 = s1;
        s1 = s;
    }
    p = s1 * s1 + s2 * s2 - ((coef * s1 * s2) >> 14);
    return p > 0 ? (uint64_t)p : 0;
}

void skin_vis_analyze(const int16_t *pcm, int frames, unsigned rate,
                      int levels[SKIN_VIS_BANDS])
{
    static unsigned coef_rate;
    static int32_t coef[SKIN_VIS_BANDS];
    int32_t sig[3][N_PTS];          /* decimation 8, 2 and 1 */
    int b, i, k;

    for (b = 0; b < SKIN_VIS_BANDS; b++)
        levels[b] = 0;
    if (frames < N_PTS * 8 || rate < 8000)
        return;

    if (rate != coef_rate)
    {
        for (b = 0; b < SKIN_VIS_BANDS; b++)
            coef[b] = 2 * cos_q14(bands[b].hz, rate / bands[b].dec);
        coef_rate = rate;
    }

    /* mono, then averaged down by 8, 2 and 1 over contiguous frames */
    for (i = 0; i < N_PTS; i++)
    {
        int32_t sum8 = 0;
        const int16_t *p8 = pcm + 2 * 8 * i;    /* frames 8i .. 8i+7 */
        const int16_t *p2 = pcm + 2 * 2 * i;    /* frames 2i, 2i+1 */
        for (k = 0; k < 8; k++)
            sum8 += (p8[2 * k] + p8[2 * k + 1]) >> 1;
        sig[0][i] = sum8 >> 3;
        sig[1][i] = (p2[0] + p2[1] + p2[2] + p2[3]) >> 2;
        sig[2][i] = (pcm[2 * i] + pcm[2 * i + 1]) >> 1;
    }

    for (b = 0; b < SKIN_VIS_BANDS; b++)
    {
        const int32_t *x = sig[bands[b].dec == 8 ? 0 : bands[b].dec == 2 ? 1 : 2];
        uint32_t amp = isqrt64(goertzel(x, coef[b])) * 2 / N_PTS;
        /* dB below full scale in tenths: 20*log10(amp/32768) */
        int db10 = ((log2_q8(amp) - 15 * 256) * 15) >> 6;
        /* treble is naturally weaker, so lift each band a little more */
        int v = ((db10 + 15 * b + 600) * 100) / 540;   /* -60..-6 dB -> 0..100 */
        levels[b] = v < 0 ? 0 : v > 100 ? 100 : v;
    }
}

#ifndef SKIN_VIS_HOST_TEST

#include "config.h"
#include "system.h"
#include "kernel.h"
#include "pcm.h"
#include "pcm_mixer.h"

/* The mixer only exposes what is left of the buffer it is playing, which
 * on some chips is often less than the 1024 frames the bass filters need.
 * So keep a rolling copy of the last 4096 frames: the mixer tells us about
 * each new buffer just before it plays it, and we copy it into a ring. */
#define RING_FRAMES  4096
#define WINDOW       (N_PTS * 8)            /* frames analysed each time */

static int16_t ring[RING_FRAMES * 2];
static volatile unsigned ring_pos;          /* frames written, ever */
static int16_t window[WINDOW * 2];

static void snap_hook(const void *start, size_t size)
{
    const int16_t *src = start;
    unsigned frames = size >> 2;
    unsigned pos, first;

    if (frames > RING_FRAMES)
    {
        src += (frames - RING_FRAMES) * 2;
        frames = RING_FRAMES;
    }
    pos = ring_pos % RING_FRAMES;
    first = MIN(frames, RING_FRAMES - pos);
    memcpy(ring + pos * 2, src, first * 4);
    if (frames > first)
        memcpy(ring, src + first * 2, (frames - first) * 4);
    ring_pos += frames;
}

static int smooth[SKIN_VIS_BANDS];
static long next_tick;
static long active_until;

static void refresh(void)
{
    int target[SKIN_VIS_BANDS] = { 0 };
    int b;

    if (mixer_channel_status(PCM_MIXER_CHAN_PLAYBACK) == CHANNEL_PLAYING)
    {
        unsigned pos;
        /* (re)attach: cheap, and a plugin may have replaced the hook */
        mixer_channel_set_buffer_hook(PCM_MIXER_CHAN_PLAYBACK, snap_hook);
        pos = ring_pos;
        if (pos >= WINDOW)
        {
            unsigned start = (pos - WINDOW) % RING_FRAMES;
            unsigned first = MIN((unsigned)WINDOW, RING_FRAMES - start);
            memcpy(window, ring + start * 2, first * 4);
            if (first < WINDOW)
                memcpy(window + first * 2, ring, (WINDOW - first) * 4);
            skin_vis_analyze(window, WINDOW, pcm_get_frequency(), target);
        }
    }

    for (b = 0; b < SKIN_VIS_BANDS; b++)
    {
        if (target[b] > smooth[b])
            smooth[b] += ((target[b] - smooth[b]) * 3 + 3) / 4;   /* quick rise */
        else
            smooth[b] = smooth[b] - 7 > target[b] ? smooth[b] - 7 : target[b];
    }
}

int skin_vis_band(int band)
{
    long now = current_tick;
    active_until = now + HZ;
    if (TIME_AFTER(now, next_tick))
    {
        next_tick = now + HZ / SKIN_VIS_FPS - 1;
        refresh();
    }
    return (band >= 0 && band < SKIN_VIS_BANDS) ? smooth[band] : 0;
}

void skin_vis_mark_active(void)
{
    active_until = current_tick + HZ;
}

bool skin_vis_active(void)
{
    return TIME_BEFORE(current_tick, active_until);
}

#endif /* !SKIN_VIS_HOST_TEST */
