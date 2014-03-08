/*
 *      fm-path-list.h
 *
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
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


#ifndef __FM_PATH_LIST_H__
#define __FM_PATH_LIST_H__

#include <glib.h>
#include <gio/gio.h>

#include "fm-list.h"
#include "fm-path.h"

G_BEGIN_DECLS

FmPathList* fm_path_list_new(void);
FmPathList* fm_path_list_new_from_uri_list(const char* uri_list);
FmPathList* fm_path_list_new_from_uris(char* const* uris);
FmPathList* fm_path_list_new_from_file_info_list(FmFileInfoList* fis);
FmPathList* fm_path_list_new_from_file_info_glist(GList* fis);
FmPathList* fm_path_list_new_from_file_info_gslist(GSList* fis);

static inline FmPathList* fm_path_list_ref(FmPathList* list)
{
    return list ? (FmPathList*)fm_list_ref((FmList*)list) : NULL;
}

static inline void fm_path_list_unref(FmPathList* list)
{
    g_return_if_fail(list);
    fm_list_unref((FmList*)list);
}

static inline guint fm_path_list_get_length(FmPathList* list)
{
    return fm_list_get_length((FmList*)list);
}
static inline gboolean fm_path_list_is_empty(FmPathList* list)
{
    return fm_list_is_empty((FmList*)list);
}
static inline FmPath* fm_path_list_peek_head(FmPathList* list)
{
    return (FmPath*)fm_list_peek_head((FmList*)list);
}
static inline GList* fm_path_list_peek_head_link(FmPathList* list)
{
    return fm_list_peek_head_link((FmList*)list);
}

static inline void fm_path_list_push_tail(FmPathList* list, FmPath* d)
{
    fm_list_push_tail((FmList*)list,d);
}

char* fm_path_list_to_uri_list(FmPathList* pl);
/* char** fm_path_list_to_uris(FmPathList* pl); */
void fm_path_list_write_uri_list(FmPathList* pl, GString* buf);

G_END_DECLS

#endif /* __FM_PATH_H__ */
