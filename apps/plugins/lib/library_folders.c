/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Library folders: which folders hold artists and which hold albums
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
#include "library_folders.h"

#define CONFIG_FILE     ROCKBOX_DIR "/library.cfg"
#define DEFAULT_LIBRARY "/Music"    /* if no start directory is set */

static const char *const role_names[] = { "artists", "albums" };

struct library_folder library_folders[LIBRARY_MAX_FOLDERS];
int library_folder_count;

/* without a trailing '/', so the root is "" */
static void set_path(char *buf, const char *path)
{
    rb->strlcpy(buf, path, MAX_PATH);
    int len = rb->strlen(buf);
    if (len > 0 && buf[len - 1] == '/')
        buf[len - 1] = '\0';
}

/* lines of "<role> <path>" */
void library_load(void)
{
    char line[MAX_PATH + 16];

    library_folder_count = 0;
    int fd = rb->open(CONFIG_FILE, O_RDONLY);
    if (fd < 0)
        return;

    while (library_folder_count < LIBRARY_MAX_FOLDERS &&
           rb->read_line(fd, line, sizeof(line)) > 0)
    {
        char *path = rb->strchr(line, ' ');
        if (!path || path[1] != '/')
            continue;
        *path++ = '\0';
        for (int role = 0; role < (int)ARRAYLEN(role_names); role++)
        {
            if (!rb->strcmp(line, role_names[role]))
            {
                struct library_folder *f = &library_folders[library_folder_count++];
                f->role = role;
                set_path(f->path, path);
                break;
            }
        }
    }
    rb->close(fd);
}

static bool library_save(void)
{
    int fd = rb->open(CONFIG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;

    bool ok = rb->fdprintf(fd, "# Library Folders, set from Listening Stats "
                               "or Recently Added\n") > 0;
    for (int i = 0; ok && i < library_folder_count; i++)
        ok = rb->fdprintf(fd, "%s %s/\n", role_names[library_folders[i].role],
                          library_folders[i].path) > 0;
    return rb->close(fd) >= 0 && ok;
}

/* offset of the first '/' in path from i on, before len, or -1 */
static int next_slash(const char *path, int i, int len)
{
    for (; i < len; i++)
    {
        if (path[i] == '/')
            return i;
    }
    return -1;
}

/* offset of the last '/' in path before len, or -1 */
static int last_slash(const char *path, int len)
{
    while (--len >= 0)
    {
        if (path[len] == '/')
            return len;
    }
    return -1;
}

bool library_is_disc_folder(const char *name, int len)
{
    int n;
    if (len >= 4 && (!rb->strncasecmp(name, "disc", 4) ||
                     !rb->strncasecmp(name, "disk", 4)))
        n = 4;
    else if (len >= 2 && !rb->strncasecmp(name, "cd", 2))
        n = 2;
    else
        return false;

    while (n < len && (name[n] == ' ' || name[n] == '_' || name[n] == '-'))
        n++;
    return n < len && name[n] >= '0' && name[n] <= '9';
}

/* the deepest marked folder the file at path is in a subfolder of */
static const struct library_folder *folder_for(const char *path, int last)
{
    const struct library_folder *best = NULL;
    int best_len = -1;

    for (int i = 0; i < library_folder_count; i++)
    {
        const struct library_folder *f = &library_folders[i];
        int len = rb->strlen(f->path);
        if (len < last && len > best_len && path[len] == '/' &&
            !rb->strncasecmp(path, f->path, len))
        {
            best = f;
            best_len = len;
        }
    }
    return best;
}

void library_artist_album(const char *path, int len, int *artist, int *album)
{
    int last = last_slash(path, len);    /* before the file name */
    const struct library_folder *f = folder_for(path, last);

    if (f)
    {
        int start = rb->strlen(f->path) + 1;
        int folder = next_slash(path, start, last + 1);
        int inside = next_slash(path, folder + 1, last + 1);

        *artist = folder;
        if (f->role == LIBRARY_ALBUMS || inside < 0 ||
            library_is_disc_folder(path + folder + 1, inside - folder - 1))
            *album = folder;
        else
            *album = inside;
        return;
    }

    /* the two folders above the track, past a disc folder */
    *album = last;
    int above = last > 0 ? last_slash(path, last) : -1;
    if (above >= 0 && library_is_disc_folder(path + above + 1, last - above - 1))
        *album = above;
    *artist = *album > 0 ? last_slash(path, *album) : -1;
}

int library_roots(struct library_folder *roots, int max)
{
    if (library_folder_count > 0)
    {
        int n = MIN(library_folder_count, max);
        rb->memcpy(roots, library_folders, n * sizeof(*roots));
        return n;
    }

    const char *start = rb->global_settings->start_directory;
    roots[0].role = LIBRARY_ARTISTS;
    if (start[0] == '/' && start[1] != '\0')
        set_path(roots[0].path, start);
    else if (rb->dir_exists(DEFAULT_LIBRARY))
        set_path(roots[0].path, DEFAULT_LIBRARY);
    else
        roots[0].path[0] = '\0';
    return 1;
}

/* ---- picking a folder ---- */

#define PICK_MAX    256
#define PICK_NAMES  8192

static char pick_dir[MAX_PATH];         /* "" for the root */
static char pick_pool[PICK_NAMES];
static char *pick_names[PICK_MAX];
static int pick_count;
static int picked;                      /* row chosen, -1 = backed out */

static int compare_names(const void *a, const void *b)
{
    return rb->strcasecmp(*(char *const *)a, *(char *const *)b);
}

/* the folders in pick_dir, sorted; some are left out if there are many */
static void read_pick_dir(void)
{
    int used = 0;

    pick_count = 0;
    DIR *dir = rb->opendir(pick_dir[0] ? pick_dir : "/");
    if (!dir)
        return;

    struct dirent *de;
    while (pick_count < PICK_MAX && (de = rb->readdir(dir)))
    {
        if (de->d_name[0] == '.' ||
            !(rb->dir_get_info(dir, de).attribute & ATTR_DIRECTORY))
            continue;
        int len = rb->strlen(de->d_name) + 1;
        if (used + len > PICK_NAMES)
            break;
        pick_names[pick_count++] = rb->strcpy(pick_pool + used, de->d_name);
        used += len;
    }
    rb->closedir(dir);
    rb->qsort(pick_names, pick_count, sizeof(*pick_names), compare_names);
}

static const char *pick_row(int item, void *data, char *buf, size_t bufsz)
{
    (void)data; (void)buf; (void)bufsz;
    return item == 0 ? "[Use This Folder]" : pick_names[item - 1];
}

static int pick_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_OK)
    {
        picked = rb->gui_synclist_get_sel_pos(lists);
        return ACTION_STD_CANCEL;
    }
    return action;
}

/* browse folders from the library root, false if backed out of */
static bool pick_folder(char *buf)
{
    struct simplelist_info info;

    pick_dir[0] = '\0';
    for (;;)
    {
        read_pick_dir();
        rb->simplelist_info_init(&info, pick_dir[0] ? pick_dir : "/",
                                 pick_count + 1, NULL);
        info.get_name = pick_row;
        info.action_callback = pick_action;
        picked = -1;
        rb->simplelist_show_list(&info);

        if (picked == 0)
        {
            rb->strlcpy(buf, pick_dir, MAX_PATH);
            return true;
        }
        if (picked > 0)
        {
            int len = rb->strlen(pick_dir);
            rb->snprintf(pick_dir + len, sizeof(pick_dir) - len, "/%s",
                         pick_names[picked - 1]);
            continue;
        }

        /* backed out: up a folder, or out of the root */
        if (!pick_dir[0])
            return false;
        *rb->strrchr(pick_dir, '/') = '\0';
    }
}

/* ---- the Library Folders screen ---- */

/* rows: the marked folders, then the two Add rows */
static const char *settings_row(int item, void *data, char *buf, size_t bufsz)
{
    (void)data;
    if (item < library_folder_count)
    {
        const struct library_folder *f = &library_folders[item];
        rb->snprintf(buf, bufsz, "%s: %s",
                     f->role == LIBRARY_ARTISTS ? "Artists" : "Albums",
                     f->path[0] ? f->path : "/");
        return buf;
    }
    return item == library_folder_count ? "Add Artists Folder..." :
                                          "Add Albums Folder...";
}

/* mark path with role, replacing any mark it had */
static void mark_folder(const char *path, int role)
{
    int i;
    for (i = 0; i < library_folder_count; i++)
    {
        if (!rb->strcasecmp(library_folders[i].path, path))
            break;
    }
    if (i == LIBRARY_MAX_FOLDERS)
    {
        rb->splashf(HZ * 2, "At most %d folders", LIBRARY_MAX_FOLDERS);
        return;
    }
    if (i == library_folder_count)
        library_folder_count++;
    library_folders[i].role = role;
    rb->strlcpy(library_folders[i].path, path, MAX_PATH);
}

bool library_settings(void)
{
    struct simplelist_info info;
    char path[MAX_PATH];
    bool changed = false;
    int selection = 0;

    library_load();
    for (;;)
    {
        rb->simplelist_info_init(&info, "Library Folders",
                                 library_folder_count + 2, NULL);
        info.get_name = settings_row;
        info.action_callback = pick_action;
        info.selection = selection;
        picked = -1;
        rb->simplelist_show_list(&info);

        if (picked < 0)
            break;
        selection = picked;

        if (picked < library_folder_count)
        {
            if (!rb->yesno_pop("Remove this folder?"))
                continue;
            library_folder_count--;
            rb->memmove(&library_folders[picked], &library_folders[picked + 1],
                        (library_folder_count - picked) * sizeof(*library_folders));
        }
        else if (pick_folder(path))
            mark_folder(path, picked == library_folder_count ?
                              LIBRARY_ARTISTS : LIBRARY_ALBUMS);
        else
            continue;

        changed = true;
        if (!library_save())
            rb->splash(HZ * 2, "Can't save Library Folders");
    }
    return changed;
}
