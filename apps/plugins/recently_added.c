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
#include "lib/library_folders.h"

/*
 * Copying music keeps each file's date but makes its folders new, so a
 * folder's date says when it arrived.
 *
 * What's listed is each folder inside the folders marked in Library
 * Folders: an artist in an artists folder (/Music/Albums/Artist/Album), a
 * soundtrack in an albums folder (/Music/OST/Soundtrack). With nothing
 * marked, the start directory or /Music counts as an artists folder. An
 * entry's date is the newest of its own and its subfolders', so a new
 * album brings its artist back to the top. Only those two levels are
 * read, never the tracks, which keeps it quick.
 *
 * Playback keeps going, as nothing here needs the audio buffer.
 */

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

/* the entries in one library folder ("" for the root), false if cut short */
static bool scan_folder(const char *folder)
{
    static long next_progress;
    char path[MAX_PATH];
    DIR *dir = rb->opendir(folder[0] ? folder : "/");
    if (!dir)
        return true;

    struct dirent *de;
    while ((de = rb->readdir(dir)))
    {
        uint32_t own = folder_date(dir, de);
        if (!own)
            continue;
        rb->snprintf(path, sizeof(path), "%s/%s", folder, de->d_name);
        add_entry(path, newest_date(path, own));

        if (TIME_AFTER(*rb->current_tick, next_progress))
        {
            rb->splashf(0, "Scanning %s...", folder[0] ? folder : "/");
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

static bool scan_library(void)
{
    struct library_folder roots[LIBRARY_MAX_FOLDERS];
    int count = library_roots(roots, ARRAYLEN(roots));

    entry_count = 0;
    for (int i = 0; i < count; i++)
    {
        if (!scan_folder(roots[i].path))
            return false;
    }
    return true;
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

static bool settings_requested;

/* Select opens the folder in the file browser, the context menu button
   opens Library Folders */
static int entry_action(int action, struct gui_synclist *lists)
{
    char path[MAX_PATH];

    if (action == ACTION_STD_CONTEXT)
    {
        settings_requested = true;
        return ACTION_STD_CANCEL;
    }
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

    library_load();
    for (;;)
    {
        if (!scan_library())
            return PLUGIN_OK;
        keep_recent();

        if (entry_count == 0)
        {
            /* the folders may be set wrong, which is the time to fix them */
            if (!rb->yesno_pop("Nothing added lately. Set Library Folders?") ||
                !library_settings())
                return PLUGIN_OK;
            continue;
        }

        rb->simplelist_info_init(&info, "Recently Added", entry_count, NULL);
        info.get_name = entry_name;
        info.action_callback = entry_action;
        settings_requested = false;
        rb->simplelist_show_list(&info);

        if (!settings_requested)
            return PLUGIN_OK;
        library_settings();
    }
}
