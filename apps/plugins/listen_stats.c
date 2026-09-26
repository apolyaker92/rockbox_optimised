/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Listening statistics from the playback log
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

#include "plugin.h"

/*
 * Reads /.rockbox/playback.log and the rotated playback_NNNN.log files,
 * whose lines are "timestamp:elapsed ms:length ms:path". Tracks, albums
 * and artists are counted by path, taking the album and artist from the
 * folders (/Music/Artist/Album/track). Only numbers and the position of
 * each name in the log are kept, names are read back when shown.
 */

#define LOG_DIR         ROCKBOX_DIR
#define PLAYLIST_DIR    "/Playlists"
#define PLAYLIST_FILE   "Most Played.m3u8"
#define MAX_LOGS        64
#define RECENT_MAX      100
#define TOP_PLAYLIST    100
#define LOG_LINE_MAX        (MAX_PATH + 64)

struct stat_entry
{
    uint32_t hash;      /* of the key, 0 = unused slot */
    uint32_t plays;
    uint32_t seconds;   /* listened */
    uint32_t last;      /* timestamp of the last play */
    uint32_t where;     /* log number << 24 | offset of the path */
    uint16_t keylen;    /* length of the key at that position */
};

struct table
{
    struct stat_entry *e;
    int size;
    int used;
    bool full;
};

enum { TRACKS, ALBUMS, ARTISTS, NUM_TABLES };

static struct table tables[NUM_TABLES];
static char log_names[MAX_LOGS][32];
static int num_logs;

static struct { uint32_t where; uint16_t keylen; uint32_t time; }
    recent[RECENT_MAX];
static int recent_count, recent_next;

static uint32_t total_plays, total_seconds, first_time, last_time;

/* sorted view of one table for the list screens */
static uint32_t *order;
static int order_count;
static int order_table;

static uint32_t hash_key(const char *s, int len)
{
    uint32_t h = 2166136261u;
    while (len--)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h ? h : 1;
}

static void count_play(int t, const char *key, int keylen, uint32_t where,
                       uint32_t secs, uint32_t time)
{
    struct table *tb = &tables[t];
    uint32_t h = hash_key(key, keylen);
    int i = h % tb->size;

    while (tb->e[i].hash && tb->e[i].hash != h)
        i = (i + 1) % tb->size;

    if (!tb->e[i].hash)
    {
        if (tb->used >= tb->size - 1)
        {
            tb->full = true;
            return;
        }
        tb->e[i].hash = h;
        tb->e[i].where = where;
        tb->e[i].keylen = keylen;
        tb->used++;
    }

    tb->e[i].plays++;
    tb->e[i].seconds += secs;
    if (time >= tb->e[i].last)
        tb->e[i].last = time;
}

/* offset of the n-th last '/' in path, or -1 */
static int nth_last_slash(const char *path, int len, int n)
{
    for (int i = len - 1; i >= 0; i--)
    {
        if (path[i] == '/' && --n == 0)
            return i;
    }
    return -1;
}

static uint32_t parse_number(char **p)
{
    uint32_t n = 0;
    while (**p >= '0' && **p <= '9')
        n = n * 10 + (*(*p)++ - '0');
    return n;
}

static void parse_line(char *line, int log, uint32_t offset)
{
    char *p = line;
    uint32_t time = parse_number(&p);
    if (*p++ != ':')
        return;
    uint32_t elapsed = parse_number(&p);
    if (*p++ != ':')
        return;
    uint32_t length = parse_number(&p);
    if (*p++ != ':' || *p != '/')
        return;

    /* count it as a play like scrobblers do */
    if (elapsed < 240000 && (length == 0 || elapsed < length / 2))
        return;

    char *path = p;
    int len = rb->strlen(path);
    while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r'))
        len--;
    if (len == 0)
        return;

    uint32_t where = ((uint32_t)log << 24) | (offset + (path - line));
    uint32_t secs = elapsed / 1000;

    count_play(TRACKS, path, len, where, secs, time);

    int album = nth_last_slash(path, len, 1);
    if (album > 0)
        count_play(ALBUMS, path, album, where, secs, time);

    int artist = nth_last_slash(path, len, 2);
    if (artist > 0)
        count_play(ARTISTS, path, artist, where, secs, time);

    recent[recent_next].where = where;
    recent[recent_next].keylen = len;
    recent[recent_next].time = time;
    recent_next = (recent_next + 1) % RECENT_MAX;
    if (recent_count < RECENT_MAX)
        recent_count++;

    total_plays++;
    total_seconds += secs;
    if (!first_time || time < first_time)
        first_time = time;
    if (time > last_time)
        last_time = time;
}

static int compare_names(const void *a, const void *b)
{
    return rb->strcmp((const char *)a, (const char *)b);
}

/* rotated logs first (oldest to newest), then the current one */
static void find_logs(void)
{
    num_logs = 0;

    DIR *dir = rb->opendir(LOG_DIR);
    if (dir)
    {
        struct dirent *de;
        while ((de = rb->readdir(dir)) && num_logs < MAX_LOGS - 1)
        {
            if (!rb->strncmp(de->d_name, "playback_", 9) &&
                rb->strlen(de->d_name) < sizeof(log_names[0]) &&
                !rb->strcasecmp(de->d_name + rb->strlen(de->d_name) - 4, ".log"))
            {
                rb->strcpy(log_names[num_logs++], de->d_name);
            }
        }
        rb->closedir(dir);
    }

    rb->qsort(log_names, num_logs, sizeof(log_names[0]), compare_names);
    rb->strcpy(log_names[num_logs++], "playback.log");
}

static bool read_logs(void)
{
    static char line[LOG_LINE_MAX];
    long next_progress = *rb->current_tick;

    for (int log = 0; log < num_logs; log++)
    {
        char path[MAX_PATH];
        rb->snprintf(path, sizeof(path), LOG_DIR "/%s", log_names[log]);
        int fd = rb->open(path, O_RDONLY);
        if (fd < 0)
            continue;

        uint32_t offset = 0;
        int n;
        while ((n = rb->read_line(fd, line, sizeof(line))) > 0)
        {
            if (line[0] != '#')
                parse_line(line, log, offset);
            offset += n;

            if (TIME_AFTER(*rb->current_tick, next_progress))
            {
                rb->splashf(0, "Reading %s...", log_names[log]);
                next_progress = *rb->current_tick + HZ / 2;
                if (rb->action_userabort(TIMEOUT_NOBLOCK))
                {
                    rb->close(fd);
                    return false;
                }
            }
        }
        rb->close(fd);
    }
    return true;
}

/* read a stored key (path or path prefix) back from its log */
static char *read_key(uint32_t where, int keylen, char *buf, int bufsz)
{
    int log = where >> 24;
    char path[MAX_PATH];

    buf[0] = '\0';
    if (log >= num_logs || keylen >= bufsz)
        return buf;

    rb->snprintf(path, sizeof(path), LOG_DIR "/%s", log_names[log]);
    int fd = rb->open(path, O_RDONLY);
    if (fd < 0)
        return buf;

    if (rb->lseek(fd, where & 0xffffff, SEEK_SET) >= 0 &&
        rb->read(fd, buf, keylen) == keylen)
        buf[keylen] = '\0';
    rb->close(fd);
    return buf;
}

/* path component counted from the end, 0 = last */
static const char *component(const char *path, int n, char *out, int outsz)
{
    int end = rb->strlen(path);
    int start;

    for (;;)
    {
        start = end;
        while (start > 0 && path[start - 1] != '/')
            start--;
        if (n-- == 0 || start == 0)
            break;
        end = start - 1;
    }

    int len = MIN(end - start, outsz - 1);
    rb->memcpy(out, path + start, len);
    out[len] = '\0';
    return out;
}

static const char *strip_ext(char *name)
{
    char *dot = rb->strrchr(name, '.');
    if (dot && dot != name)
        *dot = '\0';
    return name;
}

static int compare_order(const void *a, const void *b)
{
    const struct stat_entry *ea = &tables[order_table].e[*(const uint32_t *)a];
    const struct stat_entry *eb = &tables[order_table].e[*(const uint32_t *)b];

    if (ea->plays != eb->plays)
        return ea->plays < eb->plays ? 1 : -1;
    if (ea->seconds != eb->seconds)
        return ea->seconds < eb->seconds ? 1 : -1;
    return 0;
}

static void sort_table(int t)
{
    order_table = t;
    order_count = 0;
    for (int i = 0; i < tables[t].size; i++)
    {
        if (tables[t].e[i].hash)
            order[order_count++] = i;
    }
    rb->qsort(order, order_count, sizeof(*order), compare_order);
}

static const char *top_name(int item, void *data, char *buf, size_t bufsz)
{
    (void)data;
    struct stat_entry *e = &tables[order_table].e[order[item]];
    char key[MAX_PATH], a[64], b[64];

    read_key(e->where, e->keylen, key, sizeof(key));

    switch (order_table)
    {
        case TRACKS:
            component(key, 0, a, sizeof(a));
            rb->snprintf(buf, bufsz, "%lu  %s - %s", (unsigned long)e->plays,
                         strip_ext(a), component(key, 2, b, sizeof(b)));
            break;
        case ALBUMS:
            rb->snprintf(buf, bufsz, "%lu  %s - %s", (unsigned long)e->plays,
                         component(key, 0, a, sizeof(a)),
                         component(key, 1, b, sizeof(b)));
            break;
        default:
            rb->snprintf(buf, bufsz, "%lu  %s", (unsigned long)e->plays,
                         component(key, 0, a, sizeof(a)));
            break;
    }
    return buf;
}

static void show_top(int t, const char *title)
{
    struct simplelist_info info;

    sort_table(t);
    if (order_count == 0)
    {
        rb->splash(HZ * 2, "No plays logged yet");
        return;
    }
    rb->simplelist_info_init(&info, (char *)title, order_count, NULL);
    info.get_name = top_name;
    rb->simplelist_show_list(&info);
}

static void format_date(uint32_t time, char *buf, int bufsz)
{
    struct tm tm;
    time_t t = time;
    rb->gmtime_r(&t, &tm);
    rb->snprintf(buf, bufsz, "%04d-%02d-%02d", tm.tm_year + 1900,
                 tm.tm_mon + 1, tm.tm_mday);
}

static const char *recent_name(int item, void *data, char *buf, size_t bufsz)
{
    (void)data;
    int i = (recent_next - 1 - item + RECENT_MAX) % RECENT_MAX;
    char key[MAX_PATH], a[64], b[64], date[12];

    read_key(recent[i].where, recent[i].keylen, key, sizeof(key));
    format_date(recent[i].time, date, sizeof(date));
    component(key, 0, a, sizeof(a));
    rb->snprintf(buf, bufsz, "%s  %s - %s", date, strip_ext(a),
                 component(key, 2, b, sizeof(b)));
    return buf;
}

static void show_recent(void)
{
    struct simplelist_info info;

    if (recent_count == 0)
    {
        rb->splash(HZ * 2, "No plays logged yet");
        return;
    }
    rb->simplelist_info_init(&info, "Recently Played", recent_count, NULL);
    info.get_name = recent_name;
    rb->simplelist_show_list(&info);
}

static char summary[8][48];

static const char *summary_name(int item, void *data, char *buf, size_t bufsz)
{
    (void)data; (void)buf; (void)bufsz;
    return summary[item];
}

static void show_summary(void)
{
    struct simplelist_info info;
    char from[12], to[12];
    int n = 0;

    format_date(first_time, from, sizeof(from));
    format_date(last_time, to, sizeof(to));

    rb->snprintf(summary[n++], sizeof(summary[0]), "Plays: %lu",
                 (unsigned long)total_plays);
    rb->snprintf(summary[n++], sizeof(summary[0]), "Listened: %lu h %02lu min",
                 (unsigned long)(total_seconds / 3600),
                 (unsigned long)(total_seconds / 60 % 60));
    rb->snprintf(summary[n++], sizeof(summary[0]), "Tracks: %d%s",
                 tables[TRACKS].used, tables[TRACKS].full ? " (table full)" : "");
    rb->snprintf(summary[n++], sizeof(summary[0]), "Albums: %d",
                 tables[ALBUMS].used);
    rb->snprintf(summary[n++], sizeof(summary[0]), "Artists: %d",
                 tables[ARTISTS].used);
    if (total_plays)
    {
        rb->snprintf(summary[n++], sizeof(summary[0]), "From: %s", from);
        rb->snprintf(summary[n++], sizeof(summary[0]), "To: %s", to);
    }
    rb->snprintf(summary[n++], sizeof(summary[0]), "Logs read: %d", num_logs);

    rb->simplelist_info_init(&info, "Summary", n, NULL);
    info.get_name = summary_name;
    rb->simplelist_show_list(&info);
}

/* returns true if playback of the new playlist was started */
static bool save_playlist(void)
{
    char key[MAX_PATH];

    sort_table(TRACKS);
    if (order_count == 0)
    {
        rb->splash(HZ * 2, "No plays logged yet");
        return false;
    }

    if (!rb->dir_exists(PLAYLIST_DIR))
        rb->mkdir(PLAYLIST_DIR);

    int fd = rb->open(PLAYLIST_DIR "/" PLAYLIST_FILE,
                      O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        rb->splash(HZ * 2, "Can't write playlist");
        return false;
    }

    int count = MIN(order_count, TOP_PLAYLIST);
    for (int i = 0; i < count; i++)
    {
        struct stat_entry *e = &tables[TRACKS].e[order[i]];
        rb->fdprintf(fd, "%s\n", read_key(e->where, e->keylen, key, sizeof(key)));
    }
    rb->close(fd);

    rb->splashf(HZ, "Saved %d tracks to %s", count, PLAYLIST_FILE);

    if (rb->yesno_pop("Play it now?") &&
        rb->playlist_create(PLAYLIST_DIR "/", PLAYLIST_FILE) != -1)
    {
        rb->playlist_start(0, 0, 0);
        return true;
    }
    return false;
}

static bool setup_tables(void)
{
    size_t size;
    char *buf = rb->plugin_get_buffer(&size);

    /* tracks get 8 parts, albums 2, artists 1, plus a sort index per track */
    int tracks = size / (11 * sizeof(struct stat_entry) / 8 + sizeof(uint32_t));
    int sizes[NUM_TABLES] = { tracks, tracks / 4, tracks / 8 };

    for (int t = 0; t < NUM_TABLES; t++)
    {
        tables[t].e = (struct stat_entry *)buf;
        tables[t].size = sizes[t];
        tables[t].used = 0;
        tables[t].full = false;
        rb->memset(buf, 0, sizes[t] * sizeof(struct stat_entry));
        buf += sizes[t] * sizeof(struct stat_entry);
    }
    order = (uint32_t *)buf;
    return tracks > 16;
}

enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;

    if (!rb->global_settings->playback_log)
    {
        if (!rb->yesno_pop("Playback logging is off. Turn it on?"))
            return PLUGIN_OK;
        rb->global_settings->playback_log = true;
        rb->settings_save();
        rb->splash(HZ * 2, "Stats will start from the next track");
    }

    if (!setup_tables())
        return PLUGIN_ERROR;

    find_logs();
    if (!read_logs())
        return PLUGIN_OK;

    MENUITEM_STRINGLIST(menu, "Listening Stats", NULL,
                        "Top Tracks", "Top Albums", "Top Artists",
                        "Recently Played", "Summary",
                        "Save Most Played Playlist", "Quit");
    int selection = 0;

    for (;;)
    {
        switch (rb->do_menu(&menu, &selection, NULL, false))
        {
            case 0: show_top(TRACKS, "Top Tracks"); break;
            case 1: show_top(ALBUMS, "Top Albums"); break;
            case 2: show_top(ARTISTS, "Top Artists"); break;
            case 3: show_recent(); break;
            case 4: show_summary(); break;
            case 5:
                if (save_playlist())
                    return PLUGIN_GOTO_WPS;
                break;
            default: return PLUGIN_OK;
        }
    }
}
