/*
 *      fm-file-info-list.c
 *
 *      Copyright 2009 - 2012 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

/**
 * SECTION:fm-file-info
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fm-file-info-list.h"

static FmListFuncs fm_list_funcs =
{
    .item_ref = (gpointer (*)(gpointer))&fm_file_info_ref,
    .item_unref = (void (*)(gpointer))&fm_file_info_unref
};

/**
 * fm_file_info_list_new
 *
 * Creates a new #FmFileInfoList.
 *
 * Returns: new #FmFileInfoList object.
 */
FmFileInfoList* fm_file_info_list_new(void)
{
    return (FmFileInfoList*)fm_list_new(&fm_list_funcs);
}

/**
 * fm_file_info_list_is_same_type
 * @list: a #FmFileInfoList
 *
 * Checks if all files in the list are of the same type.
 *
 * Returns: %TRUE if all files in the list are of the same type
 */
gboolean fm_file_info_list_is_same_type(FmFileInfoList* list)
{
    /* FIXME: handle virtual files without mime-types */
    if(!fm_list_is_empty((FmList*)list))
    {
        GList* l = fm_list_peek_head_link((FmList*)list);
        FmFileInfo* fi = (FmFileInfo*)l->data;
        l = l->next;
        for(;l;l=l->next)
        {
            FmFileInfo* fi2 = (FmFileInfo*)l->data;
            if(fm_file_info_get_mime_type(fi) != fm_file_info_get_mime_type(fi2))
                return FALSE;
        }
    }
    return TRUE;
}

/**
 * fm_file_info_list_is_same_fs
 * @list: a #FmFileInfoList
 *
 * Checks if all files in the list are on the same file system.
 *
 * Returns: %TRUE if all files in the list are on the same fs.
 */
gboolean fm_file_info_list_is_same_fs(FmFileInfoList* list)
{
    if(!fm_list_is_empty((FmList*)list))
    {
        GList* l = fm_list_peek_head_link((FmList*)list);
        FmFileInfo* fi = (FmFileInfo*)l->data;
        l = l->next;
        for(;l;l=l->next)
        {
            FmFileInfo* fi2 = (FmFileInfo*)l->data;
            gboolean is_native = fm_path_is_native(fi->path);
            if(is_native != fm_path_is_native(fi2->path))
                return FALSE;
            if(is_native)
            {
                if(fi->dev != fi2->dev)
                    return FALSE;
            }
            else
            {
                if(fi->fs_id != fi2->fs_id)
                    return FALSE;
            }
        }
    }
    return TRUE;
}

