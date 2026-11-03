/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * What was added to the library lately, from the dates of its folders
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
 * Copying music keeps each file's date but makes its folders new, so a
 * folder's date says when it arrived.
 *
 * The library is the start directory, or /Music if none is set. Its
 * folders are categories, and what's listed is each folder inside one:
 * an artist in /Music/Albums/Artist/Album, a soundtrack in
 * /Music/OST/Soundtrack. An entry's date is the newest of its own and its
 * subfolders', so a new album brings its artist back to the top. Only
 * those two levels are read, never the tracks, which keeps it quick.
 *
 * Playback keeps going, as nothing here needs the audio buffer.
 */

#define DEFAULT_LIBRARY "/Music"
#define RECENT_DAYS     14
#define MAX_ENTRIES     100

struct added
{
    uint32_t time;          /* newest folder date */
    char path[MAX_PATH];
};

/* newest first */
static struct added entries[MAX_ENTRIES];
static int entry_count;

/* keep the MAX_ENTRIES newest */
static void add_entry(const char *path, uint32_t time)
{
    int i = entry_count;

    if (i == MAX_ENTRIES)
    {
        if (time <= entries[MAX_ENTRIES - 1].time)
            return;
        i--;
    }
    else
        entry_count++;

    while (i > 0 && entries[i - 1].time < time)
    {
        entries[i] = entries[i - 1];
        i--;
    }
    entries[i].time = time;
    rb->strlcpy(entries[i].path, path, sizeof(entries[i].path));
}

/* a folder's date, or 0 if it isn't one. Also skips "." and "..", and
   hidden folders like .rockbox. */
static uint32_t folder_date(DIR *dir, struct dirent *de)
{
    if (de->d_name[0] == '.')
        return 0;
    struct dirinfo info = rb->dir_get_info(dir, de);
    return (info.attribute & ATTR_DIRECTORY) ? MAX(info.mtime, 1) : 0;
}

/* the newest of a folder's date and its subfolders' */
static uint32_t newest_date(const char *path, uint32_t own)
{
    uint32_t newest = own;
    DIR *dir = rb->opendir(path);
    if (!dir)
        return newest;

    struct dirent *de;
    while ((de = rb->readdir(dir)))
        newest = MAX(newest, folder_date(dir, de));
    rb->closedir(dir);
    return newest;
}

/* the entries in one category, false if cut short */
static bool scan_category(const char *category)
{
    static long next_progress;
    char path[MAX_PATH];
    DIR *dir = rb->opendir(category);
    if (!dir)
        return true;

    struct dirent *de;
    while ((de = rb->readdir(dir)))
    {
        uint32_t own = folder_date(dir, de);
        if (!own)
            continue;
        rb->snprintf(path, sizeof(path), "%s/%s", category, de->d_name);
        add_entry(path, newest_date(path, own));

        if (TIME_AFTER(*rb->current_tick, next_progress))
        {
            rb->splashf(0, "Scanning %s...", category);
            next_progress = *rb->current_tick + HZ / 2;
            if (rb->action_userabort(TIMEOUT_NOBLOCK))
            {
                rb->closedir(dir);
                return false;
            }
        }
    }
    rb->closedir(dir);
    return true;
}

/* the start directory, or /Music, or else the whole disk */
static void library_folder(char *buf, int bufsz)
{
    const char *start = rb->global_settings->start_directory;

    if (start[0] == '/' && start[1] != '\0')
        rb->strlcpy(buf, start, bufsz);
    else if (rb->dir_exists(DEFAULT_LIBRARY))
        rb->strlcpy(buf, DEFAULT_LIBRARY, bufsz);
    else
        buf[0] = '\0';  /* the root, as paths are built with a '/' after */

    int len = rb->strlen(buf);
    if (len > 0 && buf[len - 1] == '/')
        buf[len - 1] = '\0';
}

static bool scan_library(void)
{
    char library[MAX_PATH], category[MAX_PATH];

    library_folder(library, sizeof(library));
    DIR *dir = rb->opendir(library[0] ? library : "/");
    if (!dir)
        return true;

    bool ok = true;
    struct dirent *de;
    while (ok && (de = rb->readdir(dir)))
    {
        if (!folder_date(dir, de))
            continue;
        rb->snprintf(category, sizeof(category), "%s/%s", library, de->d_name);
        ok = scan_category(category);
    }
    rb->closedir(dir);
    return ok;
}

/* drop what's older than RECENT_DAYS. A clock behind the newest folder
   was reset, as when the battery ran flat, so then count from that. */
static void keep_recent(void)
{
    uint32_t span = RECENT_DAYS * 24 * 60 * 60;
    uint32_t now = 0;
#if CONFIG_RTC
    now = rb->mktime(rb->get_time());
#endif
    uint32_t from = entry_count ? MAX(now, entries[0].time) : now;
    uint32_t cutoff = from > span ? from - span : 0;

    while (entry_count > 0 && entries[entry_count - 1].time < cutoff)
        entry_count--;
}

static const char *entry_name(int item, void *data, char *buf, size_t bufsz)
{
    (void)data;
    struct tm tm;
    time_t t = entries[item].time;
    const char *name = rb->strrchr(entries[item].path, '/') + 1;

    rb->gmtime_r(&t, &tm);
    rb->snprintf(buf, bufsz, "%04d-%02d-%02d  %s", tm.tm_year + 1900,
                 tm.tm_mon + 1, tm.tm_mday, name);
    return buf;
}

/* Select opens the folder in the file browser */
static int entry_action(int action, struct gui_synclist *lists)
{
    char path[MAX_PATH];

    if (action != ACTION_STD_OK)
        return action;

    int item = rb->gui_synclist_get_sel_pos(lists);
    rb->snprintf(path, sizeof(path), "%s/", entries[item].path);
    if (!rb->dir_exists(entries[item].path))
    {
        rb->splash(HZ * 2, "Folder not found");
        return ACTION_REDRAW;
    }

    /* the main menu opens the browser there once the plugin exits */
    rb->root_menu_browse_to(path);
    return ACTION_STD_CANCEL;
}

enum plugin_status plugin_start(const void *parameter)
{
    struct simplelist_info info;
    (void)parameter;

    if (!scan_library())
        return PLUGIN_OK;
    keep_recent();

    if (entry_count == 0)
    {
        rb->splashf(HZ * 2, "Nothing added in the last %d days", RECENT_DAYS);
        return PLUGIN_OK;
    }

    rb->simplelist_info_init(&info, "Recently Added", entry_count, NULL);
    info.get_name = entry_name;
    info.action_callback = entry_action;
    rb->simplelist_show_list(&info);
    return PLUGIN_OK;
}
