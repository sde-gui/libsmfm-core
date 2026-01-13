/*
 *      fm-file-info.h
 *
 *      Copyright 2009 - 2012 PCMan <pcman.tw@gmail.com>
 *      Copyright 2012 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifndef _FM_FILE_INFO_H_
#define _FM_FILE_INFO_H_

#include <glib.h>
#include <gio/gio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fm-icon.h"
#include "fm-list.h"
#include "fm-path.h"
#include "fm-mime-type.h"

G_BEGIN_DECLS

typedef struct _FmFileInfo FmFileInfo;

#define FILE_INFO_DEFAULT_COLOR 0xFF00FF

/* intialize the file info system */
void _fm_file_info_init();
void _fm_file_info_finalize();

void fm_log_memory_usage_for_file_info(void);

/*****************************************************************************/

FmFileInfo * fm_file_info_new();
FmFileInfo * fm_file_info_new_from_path_unfilled(FmPath * path);

FmFileInfo * fm_file_info_new_from_path(FmPath * path, GCancellable * cancellable, GError ** error);
FmFileInfo * fm_file_info_new_from_gfileinfo(FmPath * path, GFileInfo * inf);
FmFileInfo * fm_file_info_new_from_native_file(FmPath * path, const char * path_str, GError ** error);

gboolean     fm_file_info_fill(FmFileInfo * fi, GCancellable * cancellable, GError ** error);
void         fm_file_info_fill_from_gfileinfo(FmFileInfo* fi, GFileInfo* inf);
gboolean     fm_file_info_fill_from_native_file(FmFileInfo* fi, const char* path_str, GError** error);

void         fm_file_info_set_path(FmFileInfo * fi, FmPath * path);

gboolean     fm_file_info_is_filled(FmFileInfo * fi);

/*****************************************************************************/

FmFileInfo * fm_file_info_ref(FmFileInfo * fi);
void         fm_file_info_unref(FmFileInfo * fi);

/*****************************************************************************/

void fm_file_info_update(FmFileInfo* fi, FmFileInfo* src);

void fm_file_info_set_color(FmFileInfo* fi, unsigned long color);

/*****************************************************************************/

FmPath *      fm_file_info_get_path(FmFileInfo* fi);

const char *  fm_file_info_get_name(FmFileInfo* fi);
const char *  fm_file_info_get_disp_name(FmFileInfo* fi);

const char *  fm_file_info_get_desc(FmFileInfo * fi);
const char *  fm_file_info_get_disp_mtime(FmFileInfo * fi);

goffset       fm_file_info_get_size(FmFileInfo* fi);
const char *  fm_file_info_get_disp_size(FmFileInfo* fi);
goffset       fm_file_info_get_blocks(FmFileInfo * fi);

mode_t        fm_file_info_get_mode(FmFileInfo * fi);
time_t        fm_file_info_get_mtime(FmFileInfo * fi);
time_t        fm_file_info_get_atime(FmFileInfo * fi);
time_t        fm_file_info_get_ctime(FmFileInfo *fi);
FmIcon *      fm_file_info_get_icon(FmFileInfo * fi);
uid_t         fm_file_info_get_uid(FmFileInfo * fi);
gid_t         fm_file_info_get_gid(FmFileInfo * fi);
const char *  fm_file_info_get_fs_id(FmFileInfo * fi);
dev_t         fm_file_info_get_dev(FmFileInfo * fi);

gboolean      fm_file_info_icon_loaded(FmFileInfo * fi);

gboolean      fm_file_info_is_native(FmFileInfo * fi);

FmMimeType *  fm_file_info_get_mime_type(FmFileInfo * fi);

gboolean      fm_file_info_is_dir(FmFileInfo * fi);
gboolean      fm_file_info_is_directory(FmFileInfo * fi);
gboolean      fm_file_info_is_symlink(FmFileInfo * fi);
gboolean      fm_file_info_is_shortcut(FmFileInfo * fi);
gboolean      fm_file_info_is_mountable(FmFileInfo * fi);
gboolean      fm_file_info_is_image(FmFileInfo * fi);
gboolean      fm_file_info_is_text(FmFileInfo * fi);
gboolean      fm_file_info_is_desktop_entry(FmFileInfo * fi);
gboolean      fm_file_info_is_unknown_type(FmFileInfo * fi);
gboolean      fm_file_info_is_hidden(FmFileInfo * fi);
gboolean      fm_file_info_is_backup(FmFileInfo * fi);

gboolean      fm_file_info_is_executable_type(FmFileInfo * fi);
gboolean      fm_file_info_is_accessible(FmFileInfo* fi);

const char *  fm_file_info_get_target(FmFileInfo * fi);

const char *  fm_file_info_get_collate_key(FmFileInfo * fi);
const char *  fm_file_info_get_collate_key_nocasefold(FmFileInfo * fi);

gboolean      fm_file_info_can_thumbnail(FmFileInfo * fi);

unsigned long fm_file_info_get_color(FmFileInfo * fi);

/*****************************************************************************/

const char * _fm_get_default_gfile_info_query_attributes(void);

/*****************************************************************************/

#define FM_FILE_INFO(ptr)    ((FmFileInfo*)ptr)

G_END_DECLS

#endif

#include "fm-file-info-list.h"
