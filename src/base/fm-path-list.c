/*
 *      fm-path-list.c
 *
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
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
 * SECTION:fm-path
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fm-path-list.h"
#include "fm-file-info.h"
#include "fm-file.h"
#include "fm-utils.h"

#include <string.h>
#include <limits.h>
#include <glib/gi18n-lib.h>

/* path list */

static FmListFuncs funcs =
{
    .item_ref = (gpointer (*)(gpointer))&fm_path_ref,
    .item_unref = (void (*)(gpointer))&fm_path_unref
};

FmPathList* fm_path_list_new()
{
    return (FmPathList*)fm_list_new(&funcs);
}

/**
 * fm_path_list_new_from_uris
 * @uris: NULL-terminated list of URIs
 *
 * Creates a #FmPathList from @uris.
 *
 * Returns: (transfer full): new #FmPathList.
 *
 * Since: 0.1.0
 */
FmPathList* fm_path_list_new_from_uris(char* const* uris)
{
    char* const* uri;
    FmPathList* pl = fm_path_list_new();
    for(uri = uris; *uri; ++uri)
    {
        const char* puri = *uri;
        if(puri[0] != '\0') /* ensure that it's not an empty string */
        {
            FmPath* path;
            if(puri[0] == '/')
                path = fm_path_new_for_path(puri);
            else if(strstr(puri, "://"))
                path = fm_path_new_for_uri(puri);
            else /* it's not a valid path or URI */
                continue;
            fm_list_push_tail_noref((FmList*)pl, path);
        }
    }
    return pl;
}

/**
 * fm_path_list_new_from_uri_list
 * @uri_list: list of URIs separated by newline characters
 *
 * Creates a #FmPathList from @uri_list.
 *
 * Returns: (transfer full): new #FmPathList.
 *
 * Since: 0.1.0
 */
FmPathList* fm_path_list_new_from_uri_list(const char* uri_list)
{
    char** uris = g_strsplit(uri_list, "\r\n", -1);
    FmPathList* pl = fm_path_list_new_from_uris(uris);
    g_strfreev(uris);
    return pl;
}

/**
 * fm_path_list_to_uri_list
 * @pl: a path list
 *
 * Creates newline-separated list from @pl. Returned data should be freed
 * with g_free() after usage.
 *
 * Returns: (transfer full): string representation of @pl.
 *
 * Since: 0.1.0
 */
char* fm_path_list_to_uri_list(FmPathList* pl)
{
    GString* buf = g_string_sized_new(4096);
    fm_path_list_write_uri_list(pl, buf);
    return g_string_free(buf, FALSE);
}

/*
char** fm_path_list_to_uris(FmPathList* pl)
{
    if( G_LIKELY(!fm_list_is_empty(pl)) )
    {
        GList* l = fm_list_peek_head_link(pl);
        char** uris = g_new0(char*, fm_list_get_length(pl) + 1);
        for(i=0; l; ++i, l=l->next)
        {
            FmFileInfo* fi = (FmFileInfo*)l->data;
            FmPath* path = fi->path;
            char* uri = fm_path_to_uri(path);
            uris[i] = uri;
        }
    }
    return NULL;
}
*/

/**
 * fm_path_list_new_from_file_info_list
 * @fis: a file info list
 *
 * Creates a #FmPathList from @fis.
 *
 * Returns: (transfer full): new #FmPathList.
 *
 * Since: 0.1.0
 */
FmPathList* fm_path_list_new_from_file_info_list(FmFileInfoList* fis)
{
    FmPathList* list = fm_path_list_new();
    GList* l;
    for(l=fm_list_peek_head_link((FmList*)fis);l;l=l->next)
    {
        FmFileInfo* fi = (FmFileInfo*)l->data;
        fm_path_list_push_tail(list, fm_file_info_get_path(fi));
    }
    return list;
}

/**
 * fm_path_list_new_from_file_info_glist
 * @fis: (element-type FmFileInfo): list of file infos
 *
 * Creates a #FmPathList from @fis.
 *
 * Returns: (transfer full): new #FmPathList.
 *
 * Since: 0.1.0
 */
FmPathList* fm_path_list_new_from_file_info_glist(GList* fis)
{
    FmPathList* list = fm_path_list_new();
    GList* l;
    for(l=fis;l;l=l->next)
    {
        FmFileInfo* fi = (FmFileInfo*)l->data;
        fm_path_list_push_tail(list, fm_file_info_get_path(fi));
    }
    return list;
}

/**
 * fm_path_list_new_from_file_info_gslist
 * @fis: (element-type FmFileInfo): list of file infos
 *
 * Creates a #FmPathList from @fis.
 *
 * Returns: (transfer full): new #FmPathList.
 *
 * Since: 0.1.0
 */
FmPathList* fm_path_list_new_from_file_info_gslist(GSList* fis)
{
    FmPathList* list = fm_path_list_new();
    GSList* l;
    for(l=fis;l;l=l->next)
    {
        FmFileInfo* fi = (FmFileInfo*)l->data;
        fm_path_list_push_tail(list, fm_file_info_get_path(fi));
    }
    return list;
}

/**
 * fm_path_list_write_uri_list
 * @pl: a path list
 * @buf: (out): a storage for resulting list
 *
 * Creates newline-separated list of URIs from @pl.
 *
 * Since: 0.1.0
 */
void fm_path_list_write_uri_list(FmPathList* pl, GString* buf)
{
    GList* l;
    for(l = fm_path_list_peek_head_link(pl); l; l=l->next)
    {
        FmPath* path = (FmPath*)l->data;
        char* uri = fm_path_to_uri(path);
        g_string_append(buf, uri);
        g_free(uri);
        if(l->next)
            g_string_append_c(buf, '\n');
    }
}
