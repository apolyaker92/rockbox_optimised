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

#ifndef LIBRARY_FOLDERS_H
#define LIBRARY_FOLDERS_H

/*
 * Folder layouts differ too much to guess what an artist is, so the user
 * marks folders: in an artists folder each folder is an artist with its
 * albums inside, in an albums folder each folder is an album that counts
 * as its own artist (soundtracks, compilations). Tracks anywhere else
 * take the album and artist from the two folders above them. Disc
 * folders ("Disc 1", "CD2") are part of their album either way.
 *
 * The marks are kept in /.rockbox/library.cfg, shared by the plugins.
 */

#define LIBRARY_MAX_FOLDERS 8

enum library_role { LIBRARY_ARTISTS, LIBRARY_ALBUMS };

struct library_folder
{
    int role;
    char path[MAX_PATH];    /* no trailing '/', "" for the whole disk */
};

extern struct library_folder library_folders[LIBRARY_MAX_FOLDERS];
extern int library_folder_count;

void library_load(void);

/* where the artist and album folders end in a track's path, -1 if none */
void library_artist_album(const char *path, int len, int *artist, int *album);

/* the marked folders, or if there are none the start directory or /Music
   as an artists folder; returns how many */
int library_roots(struct library_folder *roots, int max);

/* the Library Folders screen, true if anything was changed */
bool library_settings(void);

#endif
