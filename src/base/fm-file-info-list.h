/*
 *      fm-file-info-list.h
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

#ifndef _FM_FILE_INFO_LIST_H_
#define _FM_FILE_INFO_LIST_H_

#include "fm-file-info.h"

G_BEGIN_DECLS

FmFileInfoList* fm_file_info_list_new();

static inline FmFileInfoList* fm_file_info_list_ref(FmFileInfoList* list)
{
    return list ? (FmFileInfoList*)fm_list_ref((FmList*)list) : NULL;
}

static inline void fm_file_info_list_unref(FmFileInfoList* list)
{
    if (list == NULL)
        return;
    fm_list_unref((FmList*)list);
}

static inline gboolean fm_file_info_list_is_empty(FmFileInfoList* list)
{
    return fm_list_is_empty((FmList*)list);
}

static inline guint fm_file_info_list_get_length(FmFileInfoList* list)
{
    return fm_list_get_length((FmList*)list);
}

static inline FmFileInfo* fm_file_info_list_peek_head(FmFileInfoList* list)
{
    return (FmFileInfo*)fm_list_peek_head((FmList*)list);
}

static inline GList* fm_file_info_list_peek_head_link(FmFileInfoList* list)
{
    return fm_list_peek_head_link((FmList*)list);
}

static inline void fm_file_info_list_push_tail(FmFileInfoList* list, FmFileInfo* d)
{
    fm_list_push_tail((FmList*)list,d);
}

static inline void fm_file_info_list_push_tail_link(FmFileInfoList* list, GList* d)
{
    fm_list_push_tail_link((FmList*)list,d);
}

static inline void fm_file_info_list_push_tail_noref(FmFileInfoList* list, FmFileInfo* d)
{
    fm_list_push_tail_noref((FmList*)list,d);
}

static inline FmFileInfo* fm_file_info_list_pop_head(FmFileInfoList* list)
{
    return (FmFileInfo*)fm_list_pop_head((FmList*)list);
}

static inline void fm_file_info_list_delete_link(FmFileInfoList* list, GList* _l)
{
    fm_list_delete_link((FmList*)list,_l);
}

static inline void fm_file_info_list_delete_link_nounref(FmFileInfoList* list, GList* _l)
{
    fm_list_delete_link_nounref((FmList*)list,_l);
}

static inline void fm_file_info_list_clear(FmFileInfoList* list)
{
    fm_list_clear((FmList*)list);
}

gboolean fm_file_info_list_is_same_type(FmFileInfoList* list);

gboolean fm_file_info_list_is_same_fs(FmFileInfoList* list);

G_END_DECLS

#endif
