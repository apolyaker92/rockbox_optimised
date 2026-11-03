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
#include "lib/library_folders.h"

/*
 * Reads /.rockbox/playback.log, whose lines are "timestamp:elapsed ms:
 * length ms:path", on top of a summary of everything played before it.
 *
 * When the log grows large the firmware renames it to playback_NNNN.log.
 * Those rotated logs are moved to playback_archive/, numbered in the order
 * they were rotated, and merged into listen_stats.dat, which holds the
 * totals of every track ever played and the most recent plays. Archived
 * logs are kept but never read again, so loading stays quick however long
 * the history gets. The summary records the last archive it holds, so a
 * merge cut short by a crash is simply done again the next time.
 *
 * Tracks, albums and artists are counted by path, taking the album and
 * artist from the folders as lib/library_folders.c explains: the two above
 * a track, unless it's in a folder marked in Library Folders as holding
 * artists or albums. Only numbers and the
 * position of each name in the files are kept, names are read back when shown.
 * The tables live in the audio buffer, so playback is stopped while the
 * stats are open, and hold over a million tracks.
 *
 * The lists can also cover only a recent period, like the last 3 months.
 * The summary only has totals, so then it is left alone and the newest
 * archives reaching into the period are read again with the current log,
 * skipping older plays. That reads only as much as the period holds.
 *
 * Summary lines:
 *   M <last archive merged>
 *   S <plays> <seconds> <first play> <last play>
 *   T <plays> <seconds> <last play> <path>     one per track
 *   R <time> <path>                            recent plays, oldest first
 */

#define LOG_DIR         ROCKBOX_DIR
#define ARCHIVE_DIR     LOG_DIR "/playback_archive"
#define SUMMARY_FILE    "listen_stats.dat"
#define SUMMARY_BACKUP  "listen_stats.bak"
#define SUMMARY_TEMP    LOG_DIR "/listen_stats.tmp"
#define PLAYLIST_DIR    "/Playlists"    /* if no catalog folder is set */
#define PLAYLIST_NAME   "Most Played"
#define MAX_LOGS        64  /* summary, archives not merged yet, current */
#define MAX_TRACKS      (1 << 20)   /* more than any library, keeps
                                       clearing the tables quick */
#define RECENT_MAX      100
#define TOP_PLAYLIST    100
#define LOG_LINE_MAX        (MAX_PATH + 64)

/* where a name (path or path prefix) is stored */
struct key
{
    uint32_t offset;
    uint16_t len;
    uint8_t log;        /* index into log_names */
};

struct stat_entry
{
    uint32_t hash;      /* of the key, 0 = unused slot */
    uint32_t check;     /* second hash of the key, so keys sharing a hash
                           are still told apart */
    uint32_t plays;
    uint32_t seconds;   /* listened */
    uint32_t last;      /* timestamp of the last play */
    struct key key;
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
static char log_names[MAX_LOGS][32];    /* in LOG_DIR, the summary is first */
static int num_logs;

static struct { struct key key; uint32_t time; } recent[RECENT_MAX];
static int recent_count, recent_next;

static uint32_t total_plays, total_seconds, first_time, last_time;
static uint32_t merged_through;     /* last archive in the summary */

/* how far back the lists go; without a clock the log has no dates */
enum { ALL_TIME, LAST_YEAR, LAST_3_MONTHS, LAST_MONTH, LAST_WEEK, NUM_PERIODS };
static const struct opt_items period_names[NUM_PERIODS] = {
    { "All Time", -1 }, { "Last 12 Months", -1 }, { "Last 3 Months", -1 },
    { "Last Month", -1 }, { "Last Week", -1 },
};
#if CONFIG_RTC
static const int period_days[NUM_PERIODS] = { 0, 365, 91, 30, 7 };
#endif
static int period = ALL_TIME;
static uint32_t cutoff;     /* plays before it are left out, 0 = none */

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

static uint32_t check_key(const char *s, int len)
{
    uint32_t h = 0x9747b28c;
    while (len--)
    {
        h = (h ^ (unsigned char)*s++) * 0x5bd1e995;
        h ^= h >> 15;
    }
    return h;
}

static void count_play(int t, const char *name, const struct key *key,
                       uint32_t plays, uint32_t secs, uint32_t time)
{
    struct table *tb = &tables[t];
    uint32_t h = hash_key(name, key->len);
    uint32_t c = check_key(name, key->len);
    int i = h % tb->size;

    while (tb->e[i].hash && (tb->e[i].hash != h || tb->e[i].check != c))
        i = (i + 1) % tb->size;

    if (!tb->e[i].hash)
    {
        if (tb->used >= tb->size - 1)
        {
            tb->full = true;
            return;
        }
        tb->e[i].hash = h;
        tb->e[i].check = c;
        tb->e[i].key = *key;
        tb->used++;
    }

    tb->e[i].plays += plays;
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

static void count_track(const char *path, struct key key,
                        uint32_t plays, uint32_t secs, uint32_t time)
{
    int len = key.len;
    int artist, album;

    count_play(TRACKS, path, &key, plays, secs, time);
    library_artist_album(path, len, &artist, &album);

    if (album > 0)
    {
        key.len = album;
        count_play(ALBUMS, path, &key, plays, secs, time);
    }

    if (artist > 0)
    {
        key.len = artist;
        count_play(ARTISTS, path, &key, plays, secs, time);
    }
}

static void add_recent(const struct key *key, uint32_t time)
{
    recent[recent_next].key = *key;
    recent[recent_next].time = time;
    recent_next = (recent_next + 1) % RECENT_MAX;
    if (recent_count < RECENT_MAX)
        recent_count++;
}

static uint32_t parse_number(char **p)
{
    uint32_t n = 0;
    while (**p >= '0' && **p <= '9')
        n = n * 10 + (*(*p)++ - '0');
    return n;
}

/* n numbers separated by spaces, returns what follows them or NULL */
static char *parse_numbers(char *p, uint32_t *v, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (*p < '0' || *p > '9')
            return NULL;
        v[i] = parse_number(&p);
        if (*p == ' ')
            p++;
        else if (*p != '\0')
            return NULL;
    }
    return p;
}

static int path_length(const char *path)
{
    int len = rb->strlen(path);
    while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r'))
        len--;
    return len;
}

static void parse_line(char *line, int log, uint32_t offset)
{
    char *p = line;
    uint32_t time = parse_number(&p);
    if (*p++ != ':' || time < cutoff)
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
    int len = path_length(path);
    if (len == 0)
        return;

    struct key key = { offset + (path - line), len, log };
    uint32_t secs = elapsed / 1000;

    count_track(path, key, 1, secs, time);
    add_recent(&key, time);

    total_plays++;
    total_seconds += secs;
    if (!first_time || time < first_time)
        first_time = time;
    if (time > last_time)
        last_time = time;
}

static void parse_summary_line(char *line, uint32_t offset)
{
    uint32_t v[4];
    char *path;

    if (line[0] == '\0' || line[1] != ' ')
        return;

    switch (line[0])
    {
        case 'M':
            if (parse_numbers(line + 2, v, 1))
                merged_through = v[0];
            return;
        case 'S':
            if (parse_numbers(line + 2, v, 4))
            {
                total_plays = v[0];
                total_seconds = v[1];
                first_time = v[2];
                last_time = v[3];
            }
            return;
        case 'T':
            path = parse_numbers(line + 2, v, 3);
            break;
        case 'R':
            path = parse_numbers(line + 2, v, 1);
            break;
        default:
            return;
    }

    if (!path || *path != '/')
        return;
    struct key key = { offset + (path - line), path_length(path), 0 };

    if (line[0] == 'T')
        count_track(path, key, v[0], v[1], v[2]);
    else
        add_recent(&key, v[0]);
}

static void add_log(const char *name)
{
    rb->strlcpy(log_names[num_logs++], name, sizeof(log_names[0]));
}

static bool read_log(int log)
{
    static char line[LOG_LINE_MAX];
    static long next_progress;
    char path[MAX_PATH];

    rb->snprintf(path, sizeof(path), LOG_DIR "/%s", log_names[log]);
    int fd = rb->open(path, O_RDONLY);
    if (fd < 0)
        return true;

    uint32_t offset = 0;
    int n;
    while ((n = rb->read_line(fd, line, sizeof(line))) > 0)
    {
        if (line[0] == '#')
            ;
        else if (log == 0 && period == ALL_TIME)    /* the summary */
            parse_summary_line(line, offset);
        else
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
    return true;
}

/* n if name is what fmt makes of it after prefix, otherwise 0 */
static uint32_t log_number(const char *name, const char *prefix,
                           const char *fmt)
{
    char made[32];
    int len = rb->strlen(prefix);

    if (rb->strncasecmp(name, prefix, len))
        return 0;
    char *s = (char *)name + len;
    uint32_t n = parse_number(&s);
    rb->snprintf(made, sizeof(made), fmt, (unsigned long)n);
    return rb->strcasecmp(name, made) ? 0 : n;
}

#define ROTATED_NAME    "playback_%04lu.log"
#define ARCHIVE_NAME    "%06lu.log"

/* add n to a sorted list, keeping the max smallest */
static void keep_smallest(uint32_t *list, int *count, int max, uint32_t n)
{
    int i = *count;

    if (i == max)
    {
        if (n >= list[max - 1])
            return;
        i--;
    }
    else
        (*count)++;

    while (i > 0 && list[i - 1] > n)
    {
        list[i] = list[i - 1];
        i--;
    }
    list[i] = n;
}

/* the archived logs after 'after', oldest first, at most max of them */
static int find_archives(uint32_t after, uint32_t *list, int max)
{
    int count = 0;
    DIR *dir = rb->opendir(ARCHIVE_DIR);
    if (!dir)
        return 0;

    struct dirent *de;
    while ((de = rb->readdir(dir)))
    {
        uint32_t n = log_number(de->d_name, "", ARCHIVE_NAME);
        if (n > after)
            keep_smallest(list, &count, max, n);
    }
    rb->closedir(dir);
    return count;
}

#if CONFIG_RTC
/* the newest archived logs, newest first, at most max of them */
static int find_newest_archives(uint32_t *list, int max)
{
    int count = 0;
    DIR *dir = rb->opendir(ARCHIVE_DIR);
    if (!dir)
        return 0;

    /* the smallest ~n are the largest n */
    struct dirent *de;
    while ((de = rb->readdir(dir)))
    {
        uint32_t n = log_number(de->d_name, "", ARCHIVE_NAME);
        if (n)
            keep_smallest(list, &count, max, ~n);
    }
    rb->closedir(dir);

    for (int i = 0; i < count; i++)
        list[i] = ~list[i];
    return count;
}

/* timestamp at the start of a log line, 0 if it isn't one */
static uint32_t line_time(char *line)
{
    char *p = line;
    uint32_t time = parse_number(&p);
    return *p == ':' ? time : 0;
}

/* size of a log in LOG_DIR and the times of its first and last plays
   (0 if none), or -1 if it can't be read */
static off_t log_times(const char *name, uint32_t *first, uint32_t *last)
{
    static char line[LOG_LINE_MAX];
    char path[MAX_PATH];

    *first = *last = 0;
    rb->snprintf(path, sizeof(path), LOG_DIR "/%s", name);
    int fd = rb->open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    off_t size = rb->filesize(fd);

    if (rb->read_line(fd, line, sizeof(line)) > 0)
        *first = line_time(line);

    /* the last line is within the last two line lengths; a seek lands
       mid-line, so the first line read after one is partial */
    off_t from = size - 2 * LOG_LINE_MAX;
    if (from > 0)
    {
        rb->lseek(fd, from, SEEK_SET);
        rb->read_line(fd, line, sizeof(line));
    }
    while (rb->read_line(fd, line, sizeof(line)) > 0)
    {
        uint32_t time = line_time(line);
        if (time)
            *last = time;
    }
    rb->close(fd);
    return size;
}
#endif

/* the highest archive number used, merged or not */
static uint32_t last_archive(void)
{
    uint32_t last = merged_through;
    DIR *dir = rb->opendir(ARCHIVE_DIR);
    if (!dir)
        return last;

    struct dirent *de;
    while ((de = rb->readdir(dir)))
    {
        uint32_t n = log_number(de->d_name, "", ARCHIVE_NAME);
        if (n > last)
            last = n;
    }
    rb->closedir(dir);
    return last;
}

/* move the logs the firmware rotated to the archive, oldest first,
   false if some could not be moved and so are left out this time */
static bool archive_rotated_logs(void)
{
    uint32_t rotated[MAX_LOGS];
    char from[MAX_PATH], to[MAX_PATH];
    int count;

    do
    {
        count = 0;
        DIR *dir = rb->opendir(LOG_DIR);
        if (!dir)
            return false;
        struct dirent *de;
        while ((de = rb->readdir(dir)))
        {
            uint32_t n = log_number(de->d_name, "playback_", ROTATED_NAME);
            if (n)
                keep_smallest(rotated, &count, MAX_LOGS, n);
        }
        rb->closedir(dir);

        if (count == 0)
            return true;
        if (!rb->dir_exists(ARCHIVE_DIR) && rb->mkdir(ARCHIVE_DIR) < 0)
            return false;

        /* never reuse a number, even if archives were deleted */
        uint32_t next = last_archive();

        for (int i = 0; i < count; i++)
        {
            rb->snprintf(from, sizeof(from), LOG_DIR "/" ROTATED_NAME,
                         (unsigned long)rotated[i]);
            rb->snprintf(to, sizeof(to), ARCHIVE_DIR "/" ARCHIVE_NAME,
                         (unsigned long)++next);
            if (rb->rename(from, to) < 0)
                return false;
        }
    } while (count == MAX_LOGS);
    return true;
}

/* the key files last read, kept open while a list is shown or saved.
   A period's names are spread over a few logs and a list jumps between
   them, so reopening one each time would make scrolling slow. */
#define KEY_FILES   4
static int key_fd[KEY_FILES] = { -1, -1, -1, -1 };
static int key_log[KEY_FILES] = { -1, -1, -1, -1 };
static int key_next;    /* the one to reuse next */

static void close_key_files(void)
{
    for (int i = 0; i < KEY_FILES; i++)
    {
        if (key_fd[i] >= 0)
            rb->close(key_fd[i]);
        key_fd[i] = key_log[i] = -1;
    }
}

static int key_file(int log)
{
    char path[MAX_PATH];

    for (int i = 0; i < KEY_FILES; i++)
    {
        if (key_log[i] == log)
            return key_fd[i];
    }

    int i = key_next;
    key_next = (key_next + 1) % KEY_FILES;
    if (key_fd[i] >= 0)
        rb->close(key_fd[i]);
    rb->snprintf(path, sizeof(path), LOG_DIR "/%s", log_names[log]);
    key_fd[i] = rb->open(path, O_RDONLY);
    key_log[i] = key_fd[i] >= 0 ? log : -1;
    return key_fd[i];
}

/* read a stored key (path or path prefix) back from its log */
static char *read_key(const struct key *key, char *buf, int bufsz)
{
    buf[0] = '\0';
    if (key->log >= num_logs || key->len >= bufsz)
        return buf;

    int fd = key_file(key->log);
    if (fd >= 0 && rb->lseek(fd, key->offset, SEEK_SET) >= 0 &&
        rb->read(fd, buf, key->len) == key->len)
        buf[key->len] = '\0';
    return buf;
}

static bool write_line(int fd, char *buf, int bufsz, int len)
{
    return len >= 0 && len < bufsz && rb->write(fd, buf, len) == len;
}

static int compare_where(const void *a, const void *b)
{
    const struct key *ka = &tables[TRACKS].e[*(const uint32_t *)a].key;
    const struct key *kb = &tables[TRACKS].e[*(const uint32_t *)b].key;
    if (ka->log != kb->log)
        return ka->log < kb->log ? -1 : 1;
    return ka->offset < kb->offset ? -1 : ka->offset > kb->offset;
}

/* write everything read so far as the summary of archives up to through */
static bool write_summary(uint32_t through)
{
    static char line[LOG_LINE_MAX];
    char key[MAX_PATH];

    int fd = rb->open(SUMMARY_TEMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;

    bool ok = write_line(fd, line, sizeof(line),
        rb->snprintf(line, sizeof(line),
                     "# Listening Stats summary, rewritten when logs merge\n"
                     "M %lu\nS %lu %lu %lu %lu\n", (unsigned long)through,
                     (unsigned long)total_plays, (unsigned long)total_seconds,
                     (unsigned long)first_time, (unsigned long)last_time));

    /* in file order so each log is read straight through */
    order_count = 0;
    for (int i = 0; i < tables[TRACKS].size; i++)
    {
        if (tables[TRACKS].e[i].hash)
            order[order_count++] = i;
    }
    rb->qsort(order, order_count, sizeof(*order), compare_where);

    for (int i = 0; ok && i < order_count; i++)
    {
        struct stat_entry *e = &tables[TRACKS].e[order[i]];
        ok = *read_key(&e->key, key, sizeof(key)) == '/' &&
            write_line(fd, line, sizeof(line),
                rb->snprintf(line, sizeof(line), "T %lu %lu %lu %s\n",
                             (unsigned long)e->plays, (unsigned long)e->seconds,
                             (unsigned long)e->last, key));
    }

    for (int i = 0; ok && i < recent_count; i++)
    {
        int r = (recent_next - recent_count + i + RECENT_MAX) % RECENT_MAX;
        ok = *read_key(&recent[r].key, key, sizeof(key)) == '/' &&
            write_line(fd, line, sizeof(line),
                rb->snprintf(line, sizeof(line), "R %lu %s\n",
                             (unsigned long)recent[r].time, key));
    }

    close_key_files();
    if (rb->close(fd) < 0)
        ok = false;
    if (!ok)
    {
        rb->remove(SUMMARY_TEMP);
        return false;
    }

    /* the old summary stays as the backup until the new one is in place */
    if (rb->file_exists(LOG_DIR "/" SUMMARY_FILE))
    {
        if (rb->rename(LOG_DIR "/" SUMMARY_FILE, LOG_DIR "/" SUMMARY_BACKUP) < 0)
            return false;
        /* names are still read from the old one */
        rb->strlcpy(log_names[0], SUMMARY_BACKUP, sizeof(log_names[0]));
    }
    return rb->rename(SUMMARY_TEMP, LOG_DIR "/" SUMMARY_FILE) >= 0;
}

static char *table_buf;     /* the audio buffer */
static int max_tracks;      /* the most it fits */

/* tables for up to tracks tracks, from the start of the buffer. A period
   gets small ones, as clearing and sorting go through the whole table. */
static void layout_tables(int tracks)
{
    char *buf = table_buf;

    /* tracks get 8 parts, albums 2, artists 1, plus a sort index per track */
    int sizes[NUM_TABLES] = { tracks, tracks / 4, tracks / 8 };

    for (int t = 0; t < NUM_TABLES; t++)
    {
        tables[t].e = (struct stat_entry *)buf;
        tables[t].size = sizes[t];
        buf += sizes[t] * sizeof(struct stat_entry);
    }
    order = (uint32_t *)buf;
}

static void reset_stats(void)
{
    for (int t = 0; t < NUM_TABLES; t++)
    {
        rb->memset(tables[t].e, 0, tables[t].size * sizeof(struct stat_entry));
        tables[t].used = 0;
        tables[t].full = false;
    }
    recent_count = recent_next = 0;
    total_plays = total_seconds = first_time = last_time = 0;
    merged_through = 0;
    num_logs = 0;
    close_key_files();
}

/* summary, then archives not merged into it yet (merging them), then
   the current log */
static bool load_all_time(void)
{
    uint32_t archives[MAX_LOGS - 2];

    cutoff = 0;
    layout_tables(max_tracks);
    for (;;)
    {
        reset_stats();

        /* no summary means the last one was cut short, use the one before */
        add_log(rb->file_exists(LOG_DIR "/" SUMMARY_FILE) ?
                SUMMARY_FILE : SUMMARY_BACKUP);
        if (!read_log(0))
            return false;

        if (!archive_rotated_logs())
            rb->splash(HZ * 2, "Can't move old logs, some left out");
        int count = find_archives(merged_through, archives, ARRAYLEN(archives));
        for (int i = 0; i < count; i++)
        {
            rb->snprintf(log_names[num_logs++], sizeof(log_names[0]),
                         "playback_archive/" ARCHIVE_NAME,
                         (unsigned long)archives[i]);
            if (!read_log(num_logs - 1))
                return false;
        }

        if (count == 0)
            break;
        if (tables[TRACKS].full || tables[ALBUMS].full || tables[ARTISTS].full)
        {
            rb->splash(HZ * 2, "Too many tracks, old logs not merged");
            break;
        }
        rb->splash(0, "Merging old logs...");
        if (!write_summary(archives[count - 1]))
        {
            rb->splash(HZ * 2, "Can't save the summary");
            break;
        }
        /* read back what was merged, then any archives left over */
    }

    add_log("playback.log");
    return read_log(num_logs - 1);
}

#if CONFIG_RTC
/* the time a play has to be after to count in the chosen period, given
   the clock's time now and the time of the newest play logged */
static uint32_t period_cutoff(uint32_t now, uint32_t newest)
{
    uint32_t span = period_days[period] * 24 * 60 * 60;

    /* a clock behind the newest play was reset, as when the battery ran
       flat, so count from that play instead. One that runs ahead can't
       be told from not having listened lately, so it is believed. */
    uint32_t from = MAX(now, newest);
    return from > span ? from - span : 0;
}

/* only the plays in the period: the newest archives reaching into it and
   the current log, leaving the summary alone */
static bool load_period(void)
{
    uint32_t archives[MAX_LOGS - 1];
    char name[32];
    uint32_t first, last, newest;

    if (!archive_rotated_logs())
        rb->splash(HZ * 2, "Can't move old logs, some left out");
    int count = find_newest_archives(archives, ARRAYLEN(archives));

    off_t size = log_times("playback.log", &first, &newest);
    off_t bytes = MAX(size, 0);
    if (!newest && count)
    {
        rb->snprintf(name, sizeof(name), "playback_archive/" ARCHIVE_NAME,
                     (unsigned long)archives[0]);
        log_times(name, &first, &newest);
    }
    cutoff = period_cutoff(rb->mktime(rb->get_time()), newest);

    /* archives are in order, so stop at the first that starts before it */
    int used = 0;
    first = UINT32_MAX;
    while (used < count && first >= cutoff)
    {
        rb->snprintf(name, sizeof(name), "playback_archive/" ARCHIVE_NAME,
                     (unsigned long)archives[used++]);
        size = log_times(name, &first, &last);
        bytes += MAX(size, 0);
    }
    if (used == ARRAYLEN(archives) && first >= cutoff)
        rb->splashf(HZ * 2, "Only the last %d logs fit", used);

    /* every play line is well over 16 bytes, so this never runs short */
    layout_tables(MIN(max_tracks, MAX(1024, (int)(bytes / 16))));
    reset_stats();

    /* oldest first, so Recently Played stays in order */
    while (used > 0)
    {
        rb->snprintf(log_names[num_logs++], sizeof(log_names[0]),
                     "playback_archive/" ARCHIVE_NAME,
                     (unsigned long)archives[--used]);
        if (!read_log(num_logs - 1))
            return false;
    }
    add_log("playback.log");
    return read_log(num_logs - 1);
}
#endif

static bool load_stats(void)
{
#if CONFIG_RTC
    if (period != ALL_TIME)
        return load_period();
#endif
    return load_all_time();
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

/* name of the artist folder a track is counted under */
static const char *track_artist(const char *path, char *out, int outsz)
{
    int artist, album;

    out[0] = '\0';
    library_artist_album(path, rb->strlen(path), &artist, &album);
    if (artist <= 0)
        return out;

    int start = nth_last_slash(path, artist, 1) + 1;
    int len = MIN(artist - start, outsz - 1);
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

    read_key(&e->key, key, sizeof(key));

    switch (order_table)
    {
        case TRACKS:
            component(key, 0, a, sizeof(a));
            rb->snprintf(buf, bufsz, "%lu  %s - %s", (unsigned long)e->plays,
                         strip_ext(a), track_artist(key, b, sizeof(b)));
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

/* set when an entry was picked to be shown in the file browser */
static bool browse_requested;

/* the file browser on a track in its album, closing the list */
static int browse_to_track(const char *path)
{
    if (path[0] != '/' || !rb->file_exists(path))
    {
        rb->splash(HZ * 2, "Track not found");
        return ACTION_REDRAW;
    }

    rb->root_menu_browse_to(path);
    browse_requested = true;
    return ACTION_STD_CANCEL;
}

/* Select opens the file browser there: inside an album or artist's
   folder, or on a track in its album */
static int top_action(int action, struct gui_synclist *lists)
{
    char key[MAX_PATH];

    if (action != ACTION_STD_OK)
        return action;

    struct stat_entry *e =
        &tables[order_table].e[order[rb->gui_synclist_get_sel_pos(lists)]];
    read_key(&e->key, key, sizeof(key) - 1);    /* room for the '/' */

    if (order_table == TRACKS)
        return browse_to_track(key);
    else
    {
        if (key[0] != '/' || !rb->dir_exists(key))
        {
            rb->splash(HZ * 2, "Folder not found");
            return ACTION_REDRAW;
        }
        rb->strlcat(key, "/", sizeof(key));
    }

    rb->root_menu_browse_to(key);
    browse_requested = true;
    return ACTION_STD_CANCEL;
}

static const char *period_name(void)
{
    return (const char *)period_names[period].string;
}

static void splash_no_plays(void)
{
    rb->splash(HZ * 2, period == ALL_TIME ? "No plays logged yet" :
                                            "No plays in this period");
}

static void show_top(int t, const char *name)
{
    struct simplelist_info info;
    char title[48];

    sort_table(t);
    if (order_count == 0)
    {
        splash_no_plays();
        return;
    }
    if (period == ALL_TIME)
        rb->strlcpy(title, name, sizeof(title));
    else
        rb->snprintf(title, sizeof(title), "%s, %s", name, period_name());
    rb->simplelist_info_init(&info, title, order_count, NULL);
    info.get_name = top_name;
    info.action_callback = top_action;
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

/* the newest play is shown first */
static int recent_index(int item)
{
    return (recent_next - 1 - item + RECENT_MAX) % RECENT_MAX;
}

static const char *recent_name(int item, void *data, char *buf, size_t bufsz)
{
    (void)data;
    int i = recent_index(item);
    char key[MAX_PATH], a[64], b[64], date[12];

    read_key(&recent[i].key, key, sizeof(key));
    format_date(recent[i].time, date, sizeof(date));
    component(key, 0, a, sizeof(a));
    rb->snprintf(buf, bufsz, "%s  %s - %s", date, strip_ext(a),
                 track_artist(key, b, sizeof(b)));
    return buf;
}

static int recent_action(int action, struct gui_synclist *lists)
{
    char key[MAX_PATH];

    if (action != ACTION_STD_OK)
        return action;

    int i = recent_index(rb->gui_synclist_get_sel_pos(lists));
    return browse_to_track(read_key(&recent[i].key, key, sizeof(key)));
}

static void show_recent(void)
{
    struct simplelist_info info;

    if (recent_count == 0)
    {
        splash_no_plays();
        return;
    }
    rb->simplelist_info_init(&info, "Recently Played", recent_count, NULL);
    info.get_name = recent_name;
    info.action_callback = recent_action;
    rb->simplelist_show_list(&info);
}

static char summary[10][48];

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

    if (period != ALL_TIME)
        rb->snprintf(summary[n++], sizeof(summary[0]), "Period: %s",
                     period_name());
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
    if (period == ALL_TIME)
    {
        rb->snprintf(summary[n++], sizeof(summary[0]), "Logs merged: %lu",
                     (unsigned long)merged_through);
        rb->snprintf(summary[n++], sizeof(summary[0]), "Logs read: %d",
                     num_logs - 1);     /* not counting the summary */
    }
    else
        rb->snprintf(summary[n++], sizeof(summary[0]), "Logs read: %d",
                     num_logs);

    rb->simplelist_info_init(&info, "Summary", n, NULL);
    info.get_name = summary_name;
    rb->simplelist_show_list(&info);
}

/* into the playlist catalog folder, ready to play from there */
static void save_playlist(void)
{
    char dir[MAX_PATH], path[MAX_PATH], key[MAX_PATH];
    const char *catalog = (const char *)rb->global_settings->playlist_catalog_dir;

    sort_table(TRACKS);
    if (order_count == 0)
    {
        splash_no_plays();
        return;
    }

    rb->strlcpy(dir, catalog[0] ? catalog : PLAYLIST_DIR, sizeof(dir));
    int len = rb->strlen(dir);
    if (len > 1 && dir[len - 1] == '/')
        dir[len - 1] = '\0';
    if (!rb->dir_exists(dir))
        rb->mkdir(dir);
    /* one per period, so a recent one doesn't replace the all-time one */
    if (period == ALL_TIME)
        rb->snprintf(path, sizeof(path), "%s/" PLAYLIST_NAME ".m3u8", dir);
    else
        rb->snprintf(path, sizeof(path), "%s/" PLAYLIST_NAME " (%s).m3u8",
                     dir, period_name());

    int fd = rb->open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        rb->splash(HZ * 2, "Can't write playlist");
        return;
    }

    int count = MIN(order_count, TOP_PLAYLIST);
    bool ok = true;
    for (int i = 0; ok && i < count; i++)
    {
        struct stat_entry *e = &tables[TRACKS].e[order[i]];
        ok = rb->fdprintf(fd, "%s\n", read_key(&e->key, key, sizeof(key))) > 0;
    }
    if (rb->close(fd) < 0 || !ok)
    {
        rb->splash(HZ * 2, "Can't write playlist");
        return;
    }

    rb->splashf(HZ * 2, "Saved %d tracks to %s", count, path);
}

static bool setup_tables(void)
{
    size_t size;

    /* stopping first also writes out the plays the firmware held back */
    rb->audio_stop();
    table_buf = rb->plugin_get_audio_buffer(&size);

    /* the parts layout_tables gives each track, rounded up so the tables
       never run past the buffer */
    max_tracks = MIN(size / ((11 * sizeof(struct stat_entry) + 7) / 8 + sizeof(uint32_t)),
                     (size_t)MAX_TRACKS);
    layout_tables(max_tracks);
    return max_tracks > 16;
}

#if CONFIG_RTC
static char period_item[32];    /* the menu shows the period chosen */

static void update_period_item(void)
{
    rb->snprintf(period_item, sizeof(period_item), "Period: %s",
                 period_name());
}

/* false if the stats couldn't be loaded for any period */
static bool choose_period(void)
{
    int old = period;

    rb->set_option("Period", &period, RB_INT, period_names, NUM_PERIODS,
                   NULL);
    update_period_item();
    if (period == old || load_stats())
        return true;

    /* cut short, go back to what was shown */
    period = old;
    update_period_item();
    return load_stats();
}
#endif

static enum plugin_status run_menu(void)
{
    MENUITEM_STRINGLIST(menu, "Listening Stats", NULL,
#if CONFIG_RTC
                        period_item,
#endif
                        "Top Tracks", "Top Albums", "Top Artists",
                        "Recently Played", "Summary",
                        "Save Most Played Playlist", "Library Folders",
                        "Quit");
    int selection = 0;

#if CONFIG_RTC
    update_period_item();
#endif
    for (;;)
    {
        int item = rb->do_menu(&menu, &selection, NULL, false);
#if CONFIG_RTC
        if (item == 0)
        {
            if (!choose_period())
                return PLUGIN_OK;
            continue;
        }
        if (item > 0)
            item--;
#endif
        switch (item)
        {
            case 0: show_top(TRACKS, "Top Tracks"); break;
            case 1: show_top(ALBUMS, "Top Albums"); break;
            case 2: show_top(ARTISTS, "Top Artists"); break;
            case 3: show_recent(); break;
            case 4: show_summary(); break;
            case 5: save_playlist(); break;
            case 6:
                /* albums and artists are worked out again from the tracks */
                if (library_settings() && !load_stats())
                    return PLUGIN_OK;
                break;
            default: return PLUGIN_OK;
        }
        if (browse_requested)
            return PLUGIN_OK;
    }
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
    library_load();

    enum plugin_status status = PLUGIN_OK;
    if (load_stats())
        status = run_menu();
    close_key_files();
    return status;
}
