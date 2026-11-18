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
 * The .lrc next to the playing file is read once when the track changes and
 * parsed in place, so showing a line costs a binary search and no disk
 * access. Lines with several time stamps, [offset:] and enhanced <mm:ss.xx>
 * word stamps are handled. A file that doesn't fit the buffer is cut at its
 * last whole line.
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
#include "config.h"
#include "skin_lyrics.h"

#if MEMORYSIZE >= 8

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "system.h"
#include "file.h"
#include "string-extra.h"
#include "metadata.h"

#define LYR_TEXT_SIZE 12288     /* bytes of .lrc kept, text and tags */
#define LYR_MAX_LINES 512       /* time stamps kept */
#define LYR_MAX_STAMPS 8        /* stamps on one line, [00:10][01:20]text */

struct lyr_line {
    uint32_t time;              /* ms */
    uint16_t off;               /* offset of the text in lyr_text */
};

static char lyr_path[MAX_PATH]; /* track the lyrics below belong to */
static char lyr_text[LYR_TEXT_SIZE];
static struct lyr_line lyr_lines[LYR_MAX_LINES];
static int lyr_count;

/* "[mm:ss]", "[mm:ss.x]", "[mm:ss.xx]" or "[mm:ss.xxx]". Returns the text
 * after the closing bracket, or NULL when p isn't a time stamp. */
static const char *parse_stamp(const char *p, uint32_t *ms)
{
    uint32_t mm = 0, ss = 0, frac = 0;
    int digits = 0, fd = 0;

    if (*p != '[')
        return NULL;
    p++;
    while (*p >= '0' && *p <= '9' && digits < 4)
        mm = mm * 10 + (*p++ - '0'), digits++;
    if (!digits || *p != ':')
        return NULL;
    p++;
    digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 3)
        ss = ss * 10 + (*p++ - '0'), digits++;
    if (!digits)
        return NULL;
    if (*p == '.' || *p == ':')
    {
        p++;
        while (*p >= '0' && *p <= '9')
        {
            if (fd < 3)
                frac = frac * 10 + (*p - '0'), fd++;
            p++;
        }
        if (!fd)
            return NULL;
        while (fd++ < 3)
            frac *= 10;
    }
    if (*p != ']')
        return NULL;
    *ms = (mm * 60 + ss) * 1000 + frac;
    return p + 1;
}

/* "<mm:ss.xx>" enhanced-LRC word stamp: returns the text after it. */
static const char *skip_word_stamp(const char *p)
{
    const char *q = p + 1;
    if (*p != '<')
        return NULL;
    while ((*q >= '0' && *q <= '9') || *q == ':' || *q == '.')
        q++;
    return (*q == '>' && q > p + 1) ? q + 1 : NULL;
}

static void add_line(uint32_t time, uint16_t off)
{
    if (lyr_count < LYR_MAX_LINES)
    {
        lyr_lines[lyr_count].time = time;
        lyr_lines[lyr_count].off = off;
        lyr_count++;
    }
}

/* Parse lyr_text[0..len) in place: the text kept for a line never reaches
 * past what has already been read, so no second buffer is needed. */
static void parse(int len)
{
    char *r = lyr_text, *end = lyr_text + len, *w = lyr_text;
    long offset_ms = 0;
    int i, j;

    if (len >= 3 && (unsigned char)r[0] == 0xEF
        && (unsigned char)r[1] == 0xBB && (unsigned char)r[2] == 0xBF)
        r += 3;                                     /* UTF-8 byte order mark */

    while (r < end && lyr_count < LYR_MAX_LINES)
    {
        char *eol = r;
        uint32_t stamps[LYR_MAX_STAMPS];
        int ns = 0;
        const char *p = r, *next;
        uint32_t t;

        while (eol < end && *eol != '\n' && *eol != '\r')
            eol++;

        while ((next = parse_stamp(p, &t)) != NULL)
        {
            if (ns < LYR_MAX_STAMPS)
                stamps[ns++] = t;
            p = next;
        }

        if (ns == 0 && strncmp(p, "[offset:", 8) == 0)
        {
            const char *q = p + 8;
            int neg = 0;
            long v = 0;
            if (*q == '+' || *q == '-')
                neg = (*q++ == '-');
            while (*q >= '0' && *q <= '9')
                v = v * 10 + (*q++ - '0');
            offset_ms = neg ? -v : v;
        }
        else if (ns > 0)
        {
            uint16_t off = (uint16_t)(w - lyr_text);
            const char *e = eol;

            while (p < e && *p == ' ')
                p++;
            while (e > p && (e[-1] == ' ' || e[-1] == '\t'))
                e--;
            while (p < e)
            {
                const char *after = skip_word_stamp(p);
                if (after && after <= e)
                    p = after;
                else
                    *w++ = *p++;
            }
            *w++ = '\0';
            for (i = 0; i < ns; i++)
                add_line(stamps[i], off);
        }

        r = eol;
        while (r < end && (*r == '\n' || *r == '\r'))
            r++;
    }

    /* apply [offset:] (positive shows the line earlier), then sort by time;
     * the files are nearly sorted already so insertion sort is fine */
    for (i = 0; i < lyr_count; i++)
    {
        long v = (long)lyr_lines[i].time - offset_ms;
        lyr_lines[i].time = v < 0 ? 0 : (uint32_t)v;
    }
    for (i = 1; i < lyr_count; i++)
    {
        struct lyr_line cur = lyr_lines[i];
        for (j = i - 1; j >= 0 && lyr_lines[j].time > cur.time; j--)
            lyr_lines[j + 1] = lyr_lines[j];
        lyr_lines[j + 1] = cur;
    }
}

static void load(const char *audio)
{
    char lrc[MAX_PATH];
    char *dot, *slash;
    int fd, n;

    strlcpy(lyr_path, audio, sizeof(lyr_path));
    lyr_count = 0;

    strlcpy(lrc, audio, sizeof(lrc));
    dot = strrchr(lrc, '.');
    slash = strrchr(lrc, '/');
    if (!dot || (slash && dot < slash))
        dot = lrc + strlen(lrc);
    if ((size_t)(dot - lrc) + 5 > sizeof(lrc))
        return;
    strcpy(dot, ".lrc");

    fd = open(lrc, O_RDONLY);
    if (fd < 0)
        return;
    n = read(fd, lyr_text, LYR_TEXT_SIZE - 1);
    close(fd);
    if (n <= 0)
        return;
    if (n == LYR_TEXT_SIZE - 1)             /* too big: keep whole lines */
    {
        while (n > 0 && lyr_text[n - 1] != '\n')
            n--;
    }
    lyr_text[n] = '\0';
    parse(n);
}

const char *skin_lyrics_line(const struct mp3entry *id3,
                             unsigned long elapsed_ms, int rel)
{
    int lo = 0, hi, idx;
    const char *text;

    if (!id3 || !id3->path[0])
        return NULL;
    if (strcmp(id3->path, lyr_path) != 0)
        load(id3->path);
    if (lyr_count == 0)
        return NULL;

    /* idx = last line whose time is <= elapsed, -1 before the first */
    hi = lyr_count;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (lyr_lines[mid].time <= elapsed_ms)
            lo = mid + 1;
        else
            hi = mid;
    }
    idx = lo - 1 + rel;
    if (idx < 0 || idx >= lyr_count)
        return NULL;
    text = lyr_text + lyr_lines[idx].off;
    return text[0] ? text : NULL;
}

bool skin_lyrics_available(const struct mp3entry *id3)
{
    if (!id3 || !id3->path[0])
        return false;
    if (strcmp(id3->path, lyr_path) != 0)
        load(id3->path);
    return lyr_count > 0;
}

static enum lyric_mode mode_now = LYRIC_MODE_AUTO;

enum lyric_mode skin_lyrics_cycle_mode(void)
{
    mode_now = (enum lyric_mode)((mode_now + 1) % 3);
    return mode_now;
}

const char *skin_lyrics_mode_text(void)
{
    return mode_now == LYRIC_MODE_LYRICS ? "LYR" :
           mode_now == LYRIC_MODE_BANDS ? "BARS" : "AUTO";
}

bool skin_lyrics_wanted(const struct mp3entry *id3)
{
    if (mode_now == LYRIC_MODE_LYRICS)
        return true;
    if (mode_now == LYRIC_MODE_BANDS)
        return false;
    return skin_lyrics_available(id3);
}

bool skin_lyrics_now(const struct mp3entry *id3, unsigned long elapsed_ms,
                     const char **cur, int *permille, const char **next)
{
    int lo = 0, hi, idx;
    const char *text;
    unsigned long t0, t1, span, est;
    int chars = 0;

    *cur = *next = NULL;
    *permille = 0;
    if (!skin_lyrics_available(id3))
        return false;

    hi = lyr_count;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (lyr_lines[mid].time <= elapsed_ms)
            lo = mid + 1;
        else
            hi = mid;
    }
    idx = lo - 1;

    if (idx + 1 < lyr_count)
    {
        text = lyr_text + lyr_lines[idx + 1].off;
        if (text[0])
            *next = text;
    }
    if (idx < 0)
        return true;

    text = lyr_text + lyr_lines[idx].off;
    if (!text[0])
        return true;                         /* a gap between lines */
    *cur = text;

    t0 = lyr_lines[idx].time;
    t1 = idx + 1 < lyr_count ? lyr_lines[idx + 1].time : t0 + 5000;
    span = t1 > t0 ? t1 - t0 : 1;
    for (; *text; text++)
        if (((unsigned char)*text & 0xC0) != 0x80)
            chars++;
    /* sung by about 85% of the way to the next line, but a line followed
     * by a long pause shouldn't crawl: allow about 90 ms a character */
    est = chars * 90;
    if (est < 1200)
        est = 1200;
    span = span * 85 / 100 < est ? span * 85 / 100 : est;
    if (span == 0)
        span = 1;
    *permille = (int)((elapsed_ms - t0) * 1000 / span);
    if (*permille > 1000)
        *permille = 1000;
    return true;
}

#else  /* not enough RAM: the tag parses but never shows anything */

const char *skin_lyrics_line(const struct mp3entry *id3,
                             unsigned long elapsed_ms, int rel)
{
    (void)id3; (void)elapsed_ms; (void)rel;
    return NULL;
}

bool skin_lyrics_available(const struct mp3entry *id3)
{
    (void)id3;
    return false;
}

static enum lyric_mode mode_now = LYRIC_MODE_AUTO;

enum lyric_mode skin_lyrics_cycle_mode(void)
{
    mode_now = (enum lyric_mode)((mode_now + 1) % 3);
    return mode_now;
}

const char *skin_lyrics_mode_text(void)
{
    return mode_now == LYRIC_MODE_LYRICS ? "LYR" :
           mode_now == LYRIC_MODE_BANDS ? "BARS" : "AUTO";
}

bool skin_lyrics_wanted(const struct mp3entry *id3)
{
    (void)id3;
    return mode_now == LYRIC_MODE_LYRICS;
}

bool skin_lyrics_now(const struct mp3entry *id3, unsigned long elapsed_ms,
                     const char **cur, int *permille, const char **next)
{
    (void)id3; (void)elapsed_ms;
    *cur = *next = NULL;
    *permille = 0;
    return false;
}

#endif
