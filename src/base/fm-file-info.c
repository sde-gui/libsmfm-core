/*
 *      fm-file-info.c
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
 * @short_description: File information cache for libfm.
 * @title: FmFileInfo
 *
 * @include: libfm/fm.h
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <grp.h> /* Query group name */
#include <pwd.h> /* Query user name */
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "fm-file-info.h"
#include "fm-file-info-deferred-load-worker.h"
#include "fm-symbol.h"
#include "fm-config.h"
#include "fm-utils.h"
#include "fm-highlighter.h"

/*****************************************************************************/

static gboolean _fm_file_info_fill_from_native_file(FmFileInfo* fi, const char* path, GError** err);
static void _fm_file_info_fill_from_gfileinfo(FmFileInfo* fi, GFileInfo* inf);

/*****************************************************************************/

#define COLLATE_USING_DISPLAY_NAME    ((FmSymbol *) -1)

static FmIcon* icon_locked_folder = NULL;

/*****************************************************************************/

/*

Evaluation of some fields of FmFileInfo deferred until the value actually needed.
When doing these deferred evaluations we acquire a lock to prevent race condition:
deferred_icon_load - lock for icon loading
deferred_mime_type_load - lock for mime type loading
deferred_fast_update - lock for any other evaluations that are "fast" by nature (i.e. not doing IO)

These locks are global, not per-object. That limits concurency level, but is much easier in implementation.

Locking order: icon-> mime_type -> fast

*/

G_LOCK_DEFINE_STATIC(deferred_icon_load);
G_LOCK_DEFINE_STATIC(deferred_mime_type_load);
G_LOCK_DEFINE_STATIC(deferred_fast_update);

#define FAST_UPDATE(check, code)\
if (G_UNLIKELY(check))\
{\
    G_LOCK(deferred_fast_update);\
    if (G_LIKELY(check))\
    {\
        code\
    }\
    G_UNLOCK(deferred_fast_update);\
}

/*****************************************************************************/

/*

FmFileInfo does not remain constant during its lifetime.
API fm_file_info_update(f1, f2) can be used to copy content of f2 into f1.
Implementation of FmFolder uses that API to update its items when underlying files updated.

The problem is that any pointer returned by fm_file_info_get_*() can become invalid at any moment,
if fm_file_info_update() is called in another thread.

The easiest way to address this is to place all the pointers into bucket and do not free them until FmFileInfo remains alive.
So as far as a caller folds a reference to FmFileInfo, it can be sure any pointer returned from fm_file_info_get_*() is valid.

*/

/*****************************************************************************/

/*
    Glib2 Atomic Operations reference:
    "While atomic has a volatile qualifier, this is a historical artifact and
    the pointer passed to it should not be volatile."
*/
#define volatile_ptr

/*****************************************************************************/

struct _FmFileInfo
{
    FmPath * volatile_ptr path; /* path of the file */

    volatile mode_t mode;
    volatile gboolean native_directory; /* set when it is a native directory or a symlink to a directory */
    volatile gboolean native_regular_file; /* set when it is a native regular file or a symlink to a native regular file */

    const char * volatile fs_id;
    volatile dev_t dev;

    volatile uid_t uid;
    volatile gid_t gid;
    volatile goffset size;
    time_t mtime;
    time_t atime;

    volatile gulong blksize;
    volatile goffset blocks;

    FmSymbol * volatile_ptr disp_name;  /* displayed name (in UTF-8) */

    /* FIXME: caching the collate key can greatly speed up sorting.
     *        However, memory usage is greatly increased!.
     *        Is there a better alternative solution?
     */
    FmSymbol * volatile_ptr collate_key_casefold; /* used to sort files by name */
    FmSymbol * volatile_ptr collate_key_nocasefold; /* the same but case-sensitive */
    FmSymbol * volatile_ptr disp_size;  /* displayed human-readable file size */
    FmSymbol * volatile_ptr disp_mtime; /* displayed last modification time */
    FmMimeType * volatile_ptr mime_type;
    FmIcon * volatile_ptr icon;

    FmSymbol * volatile_ptr target; /* target of shortcut or mountable. */

    volatile unsigned long color;

    volatile gboolean accessible; /* TRUE if can be read by user */
    volatile gboolean hidden; /* TRUE if file is hidden */
    volatile gboolean backup; /* TRUE if file is backup */

    volatile gboolean color_loaded;
    volatile gboolean from_native_file;
    volatile gboolean mime_type_load_done;

    FmSymbol * volatile_ptr native_path;

    volatile int filled;

    /*<private>*/
    volatile int n_ref;

    FmList * volatile path_bucket;
    FmList * volatile mime_type_bucket;
    FmList * volatile icon_bucket;
    FmList * volatile symbol_bucket;
};

/*****************************************************************************/

static FmListFuncs fm_list_funcs_for_path =
{
    .item_ref   = (gpointer (*)(gpointer)) &fm_path_ref,
    .item_unref = (void (*)(gpointer)) &fm_path_unref
};

static FmListFuncs fm_list_funcs_for_mime_type =
{
    .item_ref   = (gpointer (*)(gpointer)) &fm_mime_type_ref,
    .item_unref = (void (*)(gpointer)) &fm_mime_type_unref
};

static FmListFuncs fm_list_funcs_for_icon =
{
    .item_ref   = (gpointer (*)(gpointer)) &fm_icon_ref,
    .item_unref = (void (*)(gpointer)) &fm_icon_unref
};

static FmListFuncs fm_list_funcs_for_symbol =
{
    .item_ref   = (gpointer (*)(gpointer)) &fm_symbol_ref,
    .item_unref = (void (*)(gpointer)) &fm_symbol_unref,
    .item_compare = (GCompareFunc) &fm_symbol_compare_fast,
};

#define DEFINE_PUSH_VALUE(Type, type) \
static inline Fm##Type * _push_value_##type(FmFileInfo * fi, Fm##Type * value)\
{\
    return (Fm##Type *) fm_list_push_head_uniq(fi->type##_bucket, value);\
}

DEFINE_PUSH_VALUE(Path, path)
DEFINE_PUSH_VALUE(MimeType, mime_type)
DEFINE_PUSH_VALUE(Icon, icon)
DEFINE_PUSH_VALUE(Symbol, symbol)

#define SET_FIELD(field, type, value)\
do { \
    g_atomic_pointer_set(&fi->field, _push_value_##type(fi, value));\
} while (0)

#define SET_SYMBOL(field, value)\
do { \
    FmSymbol * s = fm_symbol_new(value, -1);\
    SET_FIELD(field, symbol, s);\
    fm_symbol_unref(s);\
} while (0)


#define DEFINE_GET_VALUE(Type, type) \
static inline Fm##Type * _get_value_##type(Fm##Type * volatile_ptr * ref)\
{\
    return (Fm##Type *) g_atomic_pointer_get(ref);\
}

DEFINE_GET_VALUE(Path, path)
DEFINE_GET_VALUE(MimeType, mime_type)
DEFINE_GET_VALUE(Icon, icon)
DEFINE_GET_VALUE(Symbol, symbol)

#define GET_FIELD(field, type) \
    _get_value_##type(&fi->field)

#define GET_CSTR(field) \
    fm_symbol_get_cstr(GET_FIELD(field, symbol))

/*****************************************************************************/

int file_info_total;

void fm_log_memory_usage_for_file_info(void)
{
    int total = g_atomic_int_get(&file_info_total);
    long long struct_size = sizeof(FmFileInfo);
    long long total_kb = struct_size * (long long) total / 1024;
    g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "memory usage: FmFileInfo: %lld bytes * %d items = %lld KiB",
        struct_size, total, total_kb);
}

/*****************************************************************************/

/* intialize the file info system */
void _fm_file_info_init(void)
{
    icon_locked_folder = fm_icon_from_name("folder-locked");
}

void _fm_file_info_finalize()
{
    fm_icon_unref(icon_locked_folder);
}

/*****************************************************************************/

/**
 * fm_file_info_new:
 *
 * Returns: a new FmFileInfo struct which needs to be freed with
 * fm_file_info_unref() when it's no more needed.
 */
FmFileInfo* fm_file_info_new ()
{
    g_atomic_int_inc(&file_info_total);
    FmFileInfo * fi = g_slice_new0(FmFileInfo);
    fi->n_ref = 1;
    fi->path_bucket = fm_list_new(&fm_list_funcs_for_path);
    fi->mime_type_bucket = fm_list_new(&fm_list_funcs_for_mime_type);
    fi->icon_bucket = fm_list_new(&fm_list_funcs_for_icon);
    fi->symbol_bucket = fm_list_new(&fm_list_funcs_for_symbol);
    return fi;
}

/*****************************************************************************/

/**
 * fm_file_info_new_from_path_unfilled:
 *
 * Create a new #FmFileInfo for file pointed by @path. Returned data
 * should be freed with fm_file_info_unref() after usage.
 *
 * The #FmFileInfo returned is not filled with actual file metadata.
 *
 * Returns: (transfer full): a new FmFileInfo struct which needs to be freed with
 * fm_file_info_unref() when it's no more needed.
 */
FmFileInfo * fm_file_info_new_from_path_unfilled(FmPath * path)
{
    FmFileInfo * fi = fm_file_info_new();
    fm_file_info_set_path(fi, path);
    return fi;
}

/*****************************************************************************/

/**
 * fm_file_info_set_path:
 * @fi:  A FmFileInfo struct
 * @path: a FmPath struct
 *
 * Change the path of the FmFileInfo.
 */
void fm_file_info_set_path(FmFileInfo* fi, FmPath* path)
{
    fm_return_if_fail(fi);
    SET_FIELD(path, path, path);
}


/**
 * fm_file_info_fill_from_native_file:
 * @fi:  A FmFileInfo struct
 * @path:  full path of the file
 * @err: a GError** to retrive errors
 *
 * Get file info of the specified native file and store it in
 * the #FmFileInfo struct.
 *
 * This function is not thread-safe. You should not call it on a #FmFileInfo
 * struct that is accessible from another thread. You also should not call any
 * of fm_file_info_fill_from_* functions more than once for each #FmFileInfo struct.
 *
 * Returns: TRUE if no error happens.
 */
gboolean fm_file_info_fill_from_native_file(FmFileInfo* fi, const char* path_str, GError** err)
{
    /*
        We are trying to detect and report a misuse of the API.
        If the misuse is detected, we avoid a possible race condition.
        However, there is an intentional race condition in calling fm_file_info_is_filled(),
        for performance reason. So not every misuse is detected.
    */
    /* FIXME: should we fully protect it with a mutex? */
    if (fm_file_info_is_filled(fi))
    {
        g_warning("%s is called more than once", __FUNCTION__);
        FmFileInfo * fi_src = fm_file_info_new_from_native_file(
            fm_file_info_get_path(fi), path_str, err);
        if (fi_src)
        {
            fm_file_info_update(fi, fi_src);
            fm_file_info_unref(fi_src);
            return TRUE;
        }
        return FALSE;
    }
    else
    {
        return _fm_file_info_fill_from_native_file(fi, path_str, err);
    }
}

static void _fill_from_desktop_entry(FmFileInfo * fi, const char * path)
{
    GKeyFile * kf = g_key_file_new();

    gchar * type = NULL;
    gchar * icon_name = NULL;
    gchar * title = NULL;

    if(!g_key_file_load_from_file(kf, path, 0, NULL))
        goto end;

    type = g_key_file_get_string(kf, "Desktop Entry", "Type", NULL);
    title = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
    icon_name = g_key_file_get_locale_string(kf, "Desktop Entry", "Icon", NULL, NULL);

    if (!type)
        goto end;

    if (g_strcmp0(type, "Application") == 0)
    {
        /* nothing to do */
    }
    else if (g_strcmp0(type, "Link") == 0)
    {
        /* FIXME: not implemented */
    }
    else /* unknown type - ignore it */
    {
        goto end;
    }

    if (icon_name)
    {
        FmIcon * icon = fm_icon_from_name(icon_name);
        SET_FIELD(icon, icon, icon);
        fm_icon_unref(icon);
    }

    if (title)
    {
        SET_SYMBOL(disp_name, title);
    }

end:
    g_free(type);
    g_free(title);
    g_free(icon_name);
    g_key_file_free(kf);
}

static gboolean _fm_file_info_fill_from_native_file(FmFileInfo* fi, const char* path, GError** err)
{
    struct stat st;

    if (lstat(path, &st) != 0)
    {
        g_set_error(err, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s: %s", path, g_strerror(errno));
        return FALSE;
    }

    fi->from_native_file = TRUE;
    SET_SYMBOL(native_path, path);

    fi->disp_name = NULL;
    fi->mode = st.st_mode;
    fi->mtime = st.st_mtime;
    fi->atime = st.st_atime;
    fi->size = st.st_size;
    fi->dev = st.st_dev;
    fi->uid = st.st_uid;
    fi->gid = st.st_gid;

    fi->native_directory = S_ISDIR(st.st_mode);
    fi->native_regular_file = S_ISREG(st.st_mode);

    if (S_ISLNK(st.st_mode))
    {
        struct stat _st;
        if (stat(path, &_st) == 0)
        {
            st = _st;
            fi->native_directory = S_ISDIR(st.st_mode);
            fi->native_regular_file = S_ISREG(st.st_mode);
        }
        char * target = g_file_read_link(path, NULL);
        SET_SYMBOL(target, target);
        g_free(target);
    }

    fi->accessible = (g_access(path, R_OK) == 0);

    if (!fm_config->deferred_mime_type_loading)
    {
        FmMimeType * mime_type = fm_mime_type_from_native_file(path, fm_file_info_get_disp_name(fi), &st);
        SET_FIELD(mime_type, mime_type, mime_type);
        fm_mime_type_unref(mime_type);
    }
    else
    {
        fm_file_info_deferred_load_add(fi);
        fm_file_info_deferred_load_start();
    }

    /* special handling for desktop entry files */
    if(G_UNLIKELY(fm_file_info_is_desktop_entry(fi)))
        _fill_from_desktop_entry(fi, path);

    /* By default we use the real file base name for display.
     * if the base name is not in UTF-8 encoding, we
     * need to convert it to UTF-8 for display and save its
     * UTF-8 version in fi->disp_name */
    if (!fi->disp_name)
    {
        char * dname = g_filename_display_basename(path);
        if (g_strcmp0(dname, fm_path_get_basename(fi->path)) != 0)
        {
            SET_SYMBOL(disp_name, dname);
        }
        g_free(dname);
    }

    /* files with . prefix or ~ suffix are regarded as hidden files.
     * dirs with . prefix are regarded as hidden dirs. */
    {
        const char * basename = (char*)fm_path_get_basename(fi->path);
        fi->hidden = (basename[0] == '.');
        fi->backup = (!S_ISDIR(st.st_mode) && g_str_has_suffix(basename, "~"));
    }

    return TRUE;
}

/**
 * fm_file_info_fill_from_gfileinfo:
 * @fi:  A FmFileInfo struct
 * @inf: a GFileInfo object
 *
 * Get file info from the GFileInfo object and store it in
 * the FmFileInfo struct.
 *
 * This function is not thread-safe. You should not call it on a #FmFileInfo
 * struct that is accessible from another thread. You also should not call any
 * of fm_file_info_fill_from_* functions more than once for each #FmFileInfo struct.
 */
void fm_file_info_fill_from_gfileinfo(FmFileInfo* fi, GFileInfo* inf)
{
    /*
        We are trying to detect and report a misuse of the API.
        If the misuse is detected, we avoid a possible race condition.
        However, there is an intentional race condition in calling fm_file_info_is_filled(),
        for performance reason. So not every misuse is detected.
    */
    /* FIXME: should we fully protect it with a mutex? */
    if (fm_file_info_is_filled(fi))
    {
        g_warning("%s is called more than once", __FUNCTION__);
        FmFileInfo * fi_src = fm_file_info_new_from_gfileinfo(
            fm_file_info_get_path(fi), inf);
        g_assert(fi_src != NULL);
        fm_file_info_update(fi, fi_src);
        fm_file_info_unref(fi_src);
    }
    else
    {
        _fm_file_info_fill_from_gfileinfo(fi, inf);
    }
}

static void _fm_file_info_fill_from_gfileinfo(FmFileInfo* fi, GFileInfo* inf)
{
    const char *tmp, *uri;
    GIcon* gicon;
    GFileType type;

    FmMimeType * mime_type = NULL;
    FmIcon * icon = NULL;
    char * target = NULL;

    fm_return_if_fail(fi);
    fm_return_if_fail(fi->path);

    fi->from_native_file = FALSE;

    /* if display name is the same as its name, just use it. */
    tmp = g_file_info_get_display_name(inf);
    if (g_strcmp0(tmp, fm_path_get_basename(fi->path)) != 0)
    {
        SET_SYMBOL(disp_name, tmp);
    }

    if (g_file_info_has_attribute(inf, G_FILE_ATTRIBUTE_STANDARD_SIZE))
        fi->size = g_file_info_get_size(inf);
    else
        fi->size = 0;

    if (g_file_info_has_attribute(inf, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE))
        tmp = g_file_info_get_content_type(inf);
    if (tmp)
        mime_type = fm_mime_type_from_name(tmp);

    fi->mode = g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_MODE);

    fi->uid = fi->gid = -1;
    if(g_file_info_has_attribute(inf, G_FILE_ATTRIBUTE_UNIX_UID))
        fi->uid = g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_UID);
    if(g_file_info_has_attribute(inf, G_FILE_ATTRIBUTE_UNIX_GID))
        fi->gid = g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_GID);

    type = g_file_info_get_file_type(inf);
    if(0 == fi->mode) /* if UNIX file mode is not available, compose a fake one. */
    {
        switch(type)
        {
        case G_FILE_TYPE_REGULAR:
            fi->mode |= S_IFREG;
            break;
        case G_FILE_TYPE_DIRECTORY:
            fi->mode |= S_IFDIR;
            break;
        case G_FILE_TYPE_SYMBOLIC_LINK:
            fi->mode |= S_IFLNK;
            break;
        case G_FILE_TYPE_SHORTCUT:
            break;
        case G_FILE_TYPE_MOUNTABLE:
            break;
        case G_FILE_TYPE_SPECIAL:
            if(fi->mode)
                break;
        /* if it's a special file but it doesn't have UNIX mode, compose a fake one. */
            if (g_strcmp0(tmp, "inode/chardevice")==0)
                fi->mode |= S_IFCHR;
            else if (g_strcmp0(tmp, "inode/blockdevice")==0)
                fi->mode |= S_IFBLK;
            else if (g_strcmp0(tmp, "inode/fifo")==0)
                fi->mode |= S_IFIFO;
        #ifdef S_IFSOCK
            else if (g_strcmp0(tmp, "inode/socket")==0)
                fi->mode |= S_IFSOCK;
        #endif
            break;
        case G_FILE_TYPE_UNKNOWN:
            ;
        }
    }

    if(g_file_info_has_attribute(inf, G_FILE_ATTRIBUTE_ACCESS_CAN_READ))
        fi->accessible = g_file_info_get_attribute_boolean(inf, G_FILE_ATTRIBUTE_ACCESS_CAN_READ);
    else
        /* assume it's accessible */
        fi->accessible = TRUE;

    switch(type)
    {
    case G_FILE_TYPE_MOUNTABLE:
    case G_FILE_TYPE_SHORTCUT:
        uri = g_file_info_get_attribute_string(inf, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI);
        if(uri)
        {
            if(g_str_has_prefix(uri, "file:/"))
                target = g_filename_from_uri(uri, NULL, NULL);
            else
                target = g_strdup(uri);
            if (!mime_type)
                mime_type = fm_mime_type_from_file_name(target);
        }

        if (!mime_type)
        {
            /* FIXME: is this appropriate? */
            if(type == G_FILE_TYPE_SHORTCUT)
                mime_type = fm_mime_type_ref(_fm_mime_type_get_inode_x_shortcut());
            else
                mime_type = fm_mime_type_ref(_fm_mime_type_get_inode_x_mountable());
        }
        break;
    case G_FILE_TYPE_DIRECTORY:
        if(!mime_type)
            mime_type = fm_mime_type_ref(_fm_mime_type_get_inode_directory());
        break;
    case G_FILE_TYPE_SYMBOLIC_LINK:
        uri = g_file_info_get_symlink_target(inf);
        if(uri)
        {
            if(g_str_has_prefix(uri, "file:/"))
                target = g_filename_from_uri(uri, NULL, NULL);
            else
                target = g_strdup(uri);
            if(!mime_type)
                mime_type = fm_mime_type_from_file_name(target);
        }
        /* continue with absent mime type */
        /* fall through */
    default: /* G_FILE_TYPE_UNKNOWN G_FILE_TYPE_REGULAR G_FILE_TYPE_SPECIAL */
        if (!mime_type)
        {
            uri = g_file_info_get_name(inf);
            mime_type = fm_mime_type_from_file_name(uri);
        }
    }

    /* try file-specific icon first */
    gicon = g_file_info_get_icon(inf);
    if (gicon)
        icon = fm_icon_from_gicon(gicon);
        /* g_object_unref(gicon); this is not needed since
         * g_file_info_get_icon didn't increase ref_count.
         * the object returned by g_file_info_get_icon is
         * owned by GFileInfo. */
    /* set "locked" icon on unaccesible folder */
    else if(!fi->accessible && type == G_FILE_TYPE_DIRECTORY)
        icon = fm_icon_ref(icon_locked_folder);
    else
        icon = fm_icon_ref(fm_mime_type_get_icon(mime_type));

    if (fm_path_is_native(fi->path))
    {
        fi->dev = g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_DEVICE);
    }
    else
    {
        tmp = g_file_info_get_attribute_string(inf, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
        fi->fs_id = g_intern_string(tmp);
    }

    fi->mtime = g_file_info_get_attribute_uint64(inf, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    fi->atime = g_file_info_get_attribute_uint64(inf, G_FILE_ATTRIBUTE_TIME_ACCESS);
    fi->hidden = g_file_info_get_attribute_boolean(inf, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN);
    fi->backup = g_file_info_get_attribute_boolean(inf, G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP);

    SET_FIELD(mime_type, mime_type, mime_type);
    fm_mime_type_unref(mime_type);

    SET_FIELD(icon, icon, icon);
    fm_icon_unref(icon);

    SET_SYMBOL(target, target);
    g_free(target);
}

/*****************************************************************************/

gboolean fm_file_info_is_filled(FmFileInfo * fi)
{
    return fi && fi->filled;
}

/*****************************************************************************/

/**
 * fm_file_info_new_from_gfileinfo:
 * @path:  FmPath of a file
 * @inf: a GFileInfo object
 *
 * Create a new FmFileInfo for file pointed by @path based on
 * information stored in the GFileInfo object.
 *
 * Returns: A new FmFileInfo struct which should be freed with
 * fm_file_info_unref() when no longer needed.
 */
FmFileInfo* fm_file_info_new_from_gfileinfo(FmPath* path, GFileInfo* inf)
{
    FmFileInfo* fi = fm_file_info_new();
    fm_file_info_set_path(fi, path);
    fm_file_info_fill_from_gfileinfo(fi, inf);
    return fi;
}


/**
 * fm_file_info_new_from_native_file
 * @path: (allow-none): path descriptor
 * @path_str (allow-none): full path to the file
 * @err: (allow-none) (out): pointer to receive error
 *
 * Create a new #FmFileInfo for file pointed by @path. Returned data
 * should be freed with fm_file_info_unref() after usage.
 *
 * Either @path or @path_str must not be %NULL.
 *
 * Returns: (transfer full): new file info or %NULL in case of error.
 *
 */
FmFileInfo* fm_file_info_new_from_native_file(FmPath* path, const char* path_str, GError** err)
{
    if (!path && !path_str)
    {
        /* FIXME: set err */
        return NULL;
    }

    FmFileInfo* fi = fm_file_info_new();
    char * should_be_freed = NULL;

    if (!path)
        path = fm_path_new_for_path(path_str);

    if (!path_str)
    {
        should_be_freed = fm_path_to_str(path);
        path_str = should_be_freed;
    }

    fm_file_info_set_path(fi, path);

    if (!fm_file_info_fill_from_native_file(fi, path_str, err))
    {
        fm_file_info_unref(fi);
        fi = NULL;
    }

    g_free(should_be_freed);

    return fi;
}

/*****************************************************************************/

/**
 * fm_file_info_ref:
 * @fi:  A FmFileInfo struct
 *
 * Increase reference count of the FmFileInfo struct.
 *
 * Returns: the FmFileInfo struct itself
 */
FmFileInfo* fm_file_info_ref(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi != NULL, NULL);
    g_atomic_int_inc(&fi->n_ref);
    return fi;
}

/**
 * fm_file_info_unref:
 * @fi:  A FmFileInfo struct
 *
 * Decrease reference count of the FmFileInfo struct.
 * When the last reference to the struct is released,
 * the FmFileInfo struct is freed.
 */
void fm_file_info_unref(FmFileInfo* fi)
{
    fm_return_if_fail(fi != NULL);
    /* g_debug("unref file info: %d", fi->n_ref); */
    if (g_atomic_int_dec_and_test(&fi->n_ref))
    {
        //fm_file_info_clear(fi);
        fm_list_unref(fi->path_bucket);
        fm_list_unref(fi->mime_type_bucket);
        fm_list_unref(fi->icon_bucket);
        fm_list_unref(fi->symbol_bucket);
        g_slice_free(FmFileInfo, fi);
        g_atomic_int_add(&file_info_total, -1);
    }
}

/*****************************************************************************/

/* To use from fm-file-info-deferred-load.c */
gboolean fm_file_info_only_one_ref(FmFileInfo* fi)
{
    return g_atomic_int_get(&fi->n_ref) == 1;
}

/**
 * fm_file_info_update:
 * @fi:  A FmFileInfo struct
 * @src: another FmFileInfo struct
 * 
 * Update the content of @fi by copying file info
 * stored in @src to @fi.
 */
void fm_file_info_update(FmFileInfo* fi, FmFileInfo* src)
{
    if (fi == src)
        return;

    G_LOCK(deferred_icon_load);
    G_LOCK(deferred_mime_type_load);
    G_LOCK(deferred_fast_update);

    SET_FIELD(path, path, src->path);
    SET_FIELD(mime_type, mime_type, src->mime_type);
    SET_FIELD(icon, icon, src->icon);

    fi->filled = src->filled;

    fi->mode = src->mode;
    fi->dev = src->dev;
    fi->fs_id = src->fs_id;

    fi->uid = src->uid;
    fi->gid = src->gid;
    fi->size = src->size;
    fi->mtime = src->mtime;
    fi->atime = src->atime;

    fi->blksize = src->blksize;
    fi->blocks = src->blocks;

    SET_FIELD(disp_name, symbol, src->disp_name);

    if (src->collate_key_casefold  == COLLATE_USING_DISPLAY_NAME)
        fi->collate_key_casefold = COLLATE_USING_DISPLAY_NAME;
    else
        SET_FIELD(collate_key_casefold, symbol, src->collate_key_casefold);

    if (src->collate_key_nocasefold == COLLATE_USING_DISPLAY_NAME)
        fi->collate_key_nocasefold = COLLATE_USING_DISPLAY_NAME;
    else
        SET_FIELD(collate_key_nocasefold, symbol, src->collate_key_nocasefold);

    SET_FIELD(disp_size, symbol, src->disp_size);
    SET_FIELD(disp_mtime, symbol, src->disp_mtime);

    fi->native_directory = src->native_directory;
    fi->native_regular_file = src->native_regular_file;
    fi->from_native_file = src->from_native_file;
    fi->mime_type_load_done = src->mime_type_load_done;

    SET_FIELD(native_path, symbol, src->native_path);

    G_UNLOCK(deferred_fast_update);
    G_UNLOCK(deferred_mime_type_load);
    G_UNLOCK(deferred_icon_load);
}

/*****************************************************************************/

void fm_file_info_set_color(FmFileInfo* fi, unsigned long color)
{
    fi->color = color;
    fi->color_loaded = TRUE;
}

/*****************************************************************************/

/* Logic of deferred field evaluation. Called from getters. */

static void deferred_icon_load(FmFileInfo* fi)
{
    if (G_LIKELY(fi->icon))
        return;

    G_LOCK(deferred_icon_load);

    if (fi->icon || !fi->from_native_file)
    {
        G_UNLOCK(deferred_icon_load);
        return;
    }

    const char * path = GET_CSTR(native_path);

    FmIcon * icon = NULL;

    if (fi->native_directory)
    {
        if (!fi->accessible)
            icon = fm_icon_ref(icon_locked_folder);
        else if (g_strcmp0(path, fm_get_home_dir()) == 0)
            icon = fm_icon_from_name("user-home");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP)) == 0)
            icon = fm_icon_from_name("user-desktop");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS)) == 0)
            icon = fm_icon_from_name("folder-documents");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD)) == 0)
            icon = fm_icon_from_name("folder-download");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_MUSIC)) == 0)
            icon = fm_icon_from_name("folder-music");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_PICTURES)) == 0)
            icon = fm_icon_from_name("folder-pictures");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_PUBLIC_SHARE)) == 0)
            icon = fm_icon_from_name("folder-publicshare");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_TEMPLATES)) == 0)
            icon = fm_icon_from_name("folder-templates");
        else if (g_strcmp0(path, g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS)) == 0)
            icon = fm_icon_from_name("folder-videos");
        else if(g_strcmp0(path, "/") == 0)
            icon = fm_icon_from_name("gtk-harddisk");
    }

    if (!icon)
        icon = fm_icon_ref(fm_mime_type_get_icon(fm_file_info_get_mime_type(fi)));

    SET_FIELD(icon, icon, icon);
    fm_icon_unref(icon);

    G_UNLOCK(deferred_icon_load);
}

static void deferred_mime_type_load(FmFileInfo* fi)
{
    if (G_LIKELY(fi->mime_type))
        return;

    G_LOCK(deferred_mime_type_load);

    if (fi->mime_type || fi->mime_type_load_done || !fi->from_native_file)
    {
        G_UNLOCK(deferred_mime_type_load);
        return;
    }

    /*g_debug("%s: %s", __FUNCTION__, GET_CSTR(native_path);*/

    FmMimeType * mime_type = fm_mime_type_from_native_file(
        GET_CSTR(native_path),
        fm_file_info_get_disp_name(fi), NULL);
    SET_FIELD(mime_type, mime_type, mime_type);
    fm_mime_type_unref(mime_type);

    fi->mime_type_load_done = TRUE;

    G_UNLOCK(deferred_mime_type_load);
}

/*****************************************************************************/

/* getters */

/**
 * fm_file_info_get_icon:
 * @fi:  A FmFileInfo struct
 *
 * Get the icon used to show the file in the file manager.
 *
 * Returns: a FmIcon struct. The returned FmIcon struct is
 * owned by FmFileInfo and should not be freed.
 * If you need to keep it, use fm_icon_ref() to obtain a 
 * reference.
 */
FmIcon* fm_file_info_get_icon(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    deferred_icon_load(fi);
    return GET_FIELD(icon, icon);
}

gboolean fm_file_info_icon_loaded(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    return GET_FIELD(icon, icon) != NULL;
}


/**
 * fm_file_info_get_path:
 * @fi:  A FmFileInfo struct
 *
 * Get the path of the file
 * 
 * Returns: a FmPath struct. The returned FmPath struct is
 * owned by FmFileInfo and should not be freed.
 * If you need to keep it, use fm_path_ref() to obtain a 
 * reference.
 */
FmPath* fm_file_info_get_path(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return GET_FIELD(path, path);
}

/**
 * fm_file_info_get_name:
 * @fi:  A FmFileInfo struct
 *
 * Get the base name of the file in filesystem encoding.
 *
 * Returns: a const string owned by FmFileInfo which should
 * not be freed.
 */
const char* fm_file_info_get_name(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fm_path_get_basename(fm_file_info_get_path(fi));
}

/**
 * fm_file_info_get_disp_name:
 * @fi:  A FmFileInfo struct
 *
 * Get the display name used to show the file in the file 
 * manager UI. The display name is guaranteed to be UTF-8
 * and may be different from the real file name on the 
 * filesystem.
 *
 * Returns: a const strin owned by FmFileInfo which should
 * not be freed.
 */
/* Get displayed name encoded in UTF-8 */
const char* fm_file_info_get_disp_name(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);
    return (fi->disp_name) ? GET_CSTR(disp_name) : fm_file_info_get_name(fi);
}

/**
 * fm_file_info_get_size:
 * @fi:  A FmFileInfo struct
 *
 * Returns: the size of the file in bytes.
 */
goffset fm_file_info_get_size(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fi->size;
}

/**
 * fm_file_info_get_disp_size:
 * @fi:  A FmFileInfo struct
 *
 * Get the size of the file as a human-readable string.
 * It's convinient for show the file size to the user.
 *
 * Returns: a const string owned by FmFileInfo which should
 * not be freed. (non-NULL)
 */
const char * fm_file_info_get_disp_size(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    if (S_ISREG(fi->mode))
    {
        FAST_UPDATE(!fi->disp_size,
        {
            char buf[128];
            fm_file_size_to_str(buf, sizeof(buf), fi->size, fm_config->si_unit);
            SET_SYMBOL(disp_size, buf);
        })
    }
    return GET_CSTR(disp_size);
}

/**
 * fm_file_info_get_blocks
 * @fi:  A FmFileInfo struct
 *
 * Returns: how many filesystem blocks used by the file.
 */
goffset fm_file_info_get_blocks(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fi->blocks;
}

/**
 * fm_file_info_get_mime_type:
 * @fi:  A FmFileInfo struct
 *
 * Get the mime-type of the file.
 *
 * Returns: a FmMimeType struct owned by FmFileInfo which
 * should not be freed.
 * If you need to keep it, use fm_mime_type_ref() to obtain a 
 * reference.
 */
FmMimeType* fm_file_info_get_mime_type(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    deferred_mime_type_load(fi);
    return GET_FIELD(mime_type, mime_type);
}

/**
 * fm_file_info_get_mode:
 * @fi:  A FmFileInfo struct
 *
 * Get the mode of the file. For detail about the meaning of
 * mode, see manpage of stat() and the st_mode struct field.
 *
 * Returns: mode_t value of the file as defined in POSIX struct stat.
 */
mode_t fm_file_info_get_mode(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fi->mode;
}

/**
 * fm_file_info_get_is_native:
 * @fi:  A FmFileInfo struct
 *
 * Check if the file is a native UNIX file.
 * 
 * Returns: TRUE for native UNIX files, FALSE for
 * remote filesystems or other URIs, such as 
 * trash:///, computer:///, ...etc.
 */
gboolean fm_file_info_is_native(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

	return fm_path_is_native(GET_FIELD(path, path));
}

/**
 * fm_file_info_is_directory:
 * @fi:  A FmFileInfo struct
 *
 * Returns: TRUE if the file is a directory or a link to a directory.
 */
gboolean fm_file_info_is_directory(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    if (fi->from_native_file)
    {
        return fi->native_directory;
    }

    if (S_ISDIR(fi->mode))
        return TRUE;

    return S_ISLNK(fi->mode) &&
           fm_file_info_get_mime_type(fi) &&
           (strcmp(fm_mime_type_get_type(fm_file_info_get_mime_type(fi)), "inode/directory"));
}

/**
 * fm_file_info_get_is_symlink:
 * @fi:  A FmFileInfo struct
 *
 * Check if the file is a symlink. Note that for symlinks,
 * all infos stored in FmFileInfo are actually the info of
 * their targets.
 * The only two places you can tell that is a symlink are:
 * 1. fm_file_info_get_is_symlink()
 * 2. fm_file_info_get_target() which returns the target
 * of the symlink.
 * 
 * Returns: TRUE if the file is a symlink
 */
gboolean fm_file_info_is_symlink(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    return S_ISLNK(fi->mode) ? TRUE : FALSE;
}

/**
 * fm_file_info_get_is_shortcut:
 * @fi:  A FmFileInfo struct
 *
 * Returns: TRUE if the file is a shortcut.
 * For a shortcut, read the value of fm_file_info_get_target()
 * to get the destination the shortut points to.
 * An example of shortcut type FmFileInfo is file info of
 * files in menu://applications/
 */
gboolean fm_file_info_is_shortcut(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    return fm_file_info_get_mime_type(fi) == _fm_mime_type_get_inode_x_shortcut();
}

gboolean fm_file_info_is_mountable(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    return fm_file_info_get_mime_type(fi) == _fm_mime_type_get_inode_x_mountable();
}

/**
 * fm_file_info_get_is_image:
 * @fi:  A FmFileInfo struct
 *
 * Returns: TRUE if the file is a image file (*.jpg, *.png, ...).
 */
gboolean fm_file_info_is_image(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    if (!(fm_file_info_get_mode(fi) & S_IFREG))
        return FALSE;

    /* FIXME: We had better use functions of xdg_mime to check this */
    if (!strncmp("image/", fm_mime_type_get_type(fm_file_info_get_mime_type(fi)), 6))
        return TRUE;
    return FALSE;
}

/**
 * fm_file_info_get_is_text:
 * @fi:  A FmFileInfo struct
 *
 * Returns: TRUE if the file is a plain text file.
 */
gboolean fm_file_info_is_text(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    if(g_content_type_is_a(fm_mime_type_get_type(fm_file_info_get_mime_type(fi)), "text/plain"))
        return TRUE;
    return FALSE;
}

/**
 * fm_file_info_get_is_desktop_entry:
 * @fi:  A FmFileInfo struct
 *
 * Returns: TRUE if the file is a desktop entry file.
 */
gboolean fm_file_info_is_desktop_entry(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    if (fi->from_native_file)
    {
        if (!fi->native_regular_file)
            return FALSE;
        const char * target = GET_CSTR(target);
        const char * path = target ? target : GET_CSTR(native_path);
        if (!g_str_has_suffix(path, ".desktop"))
            return FALSE;
    }
    return fm_file_info_get_mime_type(fi) == _fm_mime_type_get_application_x_desktop();
}

/**
 * fm_file_info_get_is_unknown_type:
 * @fi:  A FmFileInfo struct
 *
 * Returns: TRUE if the mime type of the file cannot be
 * recognized.
 */
gboolean fm_file_info_is_unknown_type(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    return g_content_type_is_unknown(fm_mime_type_get_type(fm_file_info_get_mime_type(fi)));
}

/**
 * fm_file_info_get_is_executable_type:
 * @fi:  A FmFileInfo struct
 *
 * Note that the function only check if the file seems
 * to be an executable file. It does not check if the
 * user really has the permission to execute the file or
 * if the executable bit of the file is set.
 * To check if a file is really executable by the current
 * user, you may need to call POSIX access() or euidaccess().
 * 
 * Returns: TRUE if the file is a kind of executable file,
 * such as shell script, python script, perl script, or 
 * binary executable file.
 */
/* full path of the file is required by this function */
gboolean fm_file_info_is_executable_type(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    if(strncmp(fm_mime_type_get_type(fm_file_info_get_mime_type(fi)), "text/", 5) == 0)
    { /* g_content_type_can_be_executable reports text files as executables too */
        /* We don't execute remote files nor files in trash */
        if(fm_path_is_native(fi->path) && (fi->mode & (S_IXOTH|S_IXGRP|S_IXUSR)))
        { /* it has executable bits so lets check shell-bang */
            char *path = fm_path_to_str(fi->path);
            int fd = open(path, O_RDONLY);
            g_free(path);
            if(fd >= 0)
            {
                char buf[2];
                ssize_t rdlen = read(fd, &buf, 2);
                close(fd);
                if(rdlen == 2 && buf[0] == '#' && buf[1] == '!')
                    return TRUE;
            }
        }
        return FALSE;
    }
    return g_content_type_can_be_executable(fm_mime_type_get_type(fm_file_info_get_mime_type(fi)));
}

/**
 * fm_file_info_is_accessible
 * @fi: a file info descriptor
 *
 * Checks if the user has read access to file or directory @fi.
 *
 * Returns: %TRUE if @fi is accessible for user.
 *
 * Since: 1.0.1
 */
gboolean fm_file_info_is_accessible(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    return fi->accessible;
}

/**
 * fm_file_info_get_is_hidden:
 * @fi:  A FmFileInfo struct
 *
 * Files treated as hidden files are filenames with dot prefix
 * or ~ suffix.
 * 
 * Returns: TRUE if the file is a hidden file.
 */
gboolean fm_file_info_is_hidden(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    return (fi->hidden ||
            /* bug #3416724: backup and hidden files should be distinguishable */
            (fm_config->backup_as_hidden && fi->backup));
}

/**
 * fm_file_info_get_can_thumbnail:
 * @fi:  A FmFileInfo struct
 *
 * Returns: TRUE if the the file manager can try to 
 * generate a thumbnail for the file.
 */
gboolean fm_file_info_can_thumbnail(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, FALSE);

    if (fi->size == 0)
        return FALSE;

    if (!(fi->mode & S_IFREG)) /* We cannot use S_ISREG here as this exclude all symlinks */
        return FALSE;

    if (!fi->mime_type) /* If mime type not loaded yet, assume it can have thumbnail. */
        return TRUE;

    if (fm_file_info_is_desktop_entry(fi) || fm_file_info_is_unknown_type(fi))
        return FALSE;

    return TRUE;
}


/**
 * fm_file_info_get_collate_key:
 * @fi:  A FmFileInfo struct
 *
 * Get the collate key used for locale-dependent
 * filename sorting. The keys of different files 
 * can be compared with strcmp() directly.
 * 
 * Returns: a const string owned by FmFileInfo which should
 * not be freed.
 */
const char * fm_file_info_get_collate_key(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    /* create a collate key on demand, if we don't have one */
    FAST_UPDATE(!fi->collate_key_casefold,
    {
        const char * disp_name = fm_file_info_get_disp_name(fi);
        char * casefold = g_utf8_casefold(disp_name, -1);
        char * collate = g_utf8_collate_key_for_filename(casefold, -1);
        g_free(casefold);
        if (strcmp(collate, disp_name))
            SET_SYMBOL(collate_key_casefold, collate);
        else
            fi->collate_key_casefold = COLLATE_USING_DISPLAY_NAME;
        g_free(collate);
    })

    /* if the collate key is the same as the display name, 
     * just return the display name instead. */
    if (fi->collate_key_casefold == COLLATE_USING_DISPLAY_NAME)
        return fm_file_info_get_disp_name(fi);

    return GET_CSTR(collate_key_casefold);
}

/**
 * fm_file_info_get_collate_key_nocasefold
 * @fi: a #FmFileInfo struct
 *
 * Get the collate key used for locale-dependent filename sorting but
 * in case-sensitive manner. The keys of different files can be compared
 * with strcmp() directly. Returned data are owned by FmFileInfo and
 * should be not freed by caller.
 *
 * See also: fm_file_info_get_collate_key().
 *
 * Returns: collate string.
 *
 * Since: 1.0.2
 */
const char * fm_file_info_get_collate_key_nocasefold(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    FAST_UPDATE(!fi->collate_key_nocasefold,
    {
        const char * disp_name = fm_file_info_get_disp_name(fi);
        char * collate = g_utf8_collate_key_for_filename(disp_name, -1);
        if (strcmp(collate, disp_name))
            SET_SYMBOL(collate_key_nocasefold, collate);
        else
            fi->collate_key_nocasefold = COLLATE_USING_DISPLAY_NAME;
        g_free(collate);
    })

    /* if the collate key is the same as the display name, 
     * just return the display name instead. */
    if (fi->collate_key_nocasefold == COLLATE_USING_DISPLAY_NAME)
        return fm_file_info_get_disp_name(fi);

    return GET_CSTR(collate_key_nocasefold);
}

/**
 * fm_file_info_get_target:
 * @fi:  A FmFileInfo struct
 *
 * Get the target of a symlink or a shortcut.
 * 
 * Returns: a const string owned by FmFileInfo which should
 * not be freed. NULL if the file is not a symlink or
 * shortcut.
 */
const char * fm_file_info_get_target(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return GET_CSTR(target);
}

/**
 * fm_file_info_get_desc:
 * @fi:  A FmFileInfo struct
 * 
 * Get a human-readable description for the file.
 * 
 * Returns: a const string owned by FmFileInfo which should
 * not be freed.
 */
const char * fm_file_info_get_desc(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    /* FIXME: how to handle descriptions for virtual files without mime-tyoes? */
    return fm_file_info_get_mime_type(fi) ? fm_mime_type_get_desc(fm_file_info_get_mime_type(fi)) : NULL;
}

/**
 * fm_file_info_get_disp_mtime:
 * @fi:  A FmFileInfo struct
 * 
 * Get a human-readable string for showing file modification
 * time in the UI.
 * 
 * Returns: a const string owned by FmFileInfo which should
 * not be freed.
 */
const char * fm_file_info_get_disp_mtime(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    if (fi->mtime > 0)
    {
        FAST_UPDATE(!fi->disp_mtime,
        {
            char buf[128];
            strftime(buf, sizeof(buf),
                      "%x %R",
                      localtime(&fi->mtime));
            SET_SYMBOL(disp_mtime, buf);
        })
    }
    return GET_CSTR(disp_mtime);
}

/**
 * fm_file_info_get_mtime:
 * @fi:  A FmFileInfo struct
 * 
 * Returns: file modification time.
 */
time_t fm_file_info_get_mtime(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fi->mtime;
}

/**
 * fm_file_info_get_mtime:
 * @fi:  A FmFileInfo struct
 * 
 * Returns: file access time.
 */
time_t fm_file_info_get_atime(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fi->atime;
}

/**
 * fm_file_info_get_uid:
 * @fi:  A FmFileInfo struct
 * 
 * Returns: user id (uid) of the file owner.
 */
uid_t fm_file_info_get_uid(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, -1);

    return fi->uid;
}

/**
 * fm_file_info_get_gid:
 * @fi:  A FmFileInfo struct
 * 
 * Returns: group id (gid) of the file owner.
 */
gid_t fm_file_info_get_gid(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, -1);

    return fi->gid;
}


/**
 * fm_file_info_get_fs_id:
 * @fi:  A FmFileInfo struct
 * 
 * Get the filesystem id string
 * This is only applicable when the file is on a remote
 * filesystem. e.g. fm_file_info_is_native() returns FALSE.
 * 
 * Returns: a const string owned by FmFileInfo which should
 * not be freed.
 */
const char * fm_file_info_get_fs_id(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fi->fs_id;
}

/**
 * fm_file_info_get_dev:
 * @fi:  A FmFileInfo struct
 * 
 * Get the filesystem device id (POSIX dev_t)
 * This is only applicable when the file is native.
 * e.g. fm_file_info_is_native() returns TRUE.
 * 
 * Returns: device id (POSIX dev_t, st_dev member of 
 * struct stat).
 */
dev_t fm_file_info_get_dev(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    return fi->dev;
}

unsigned long fm_file_info_get_color(FmFileInfo* fi)
{
    fm_return_val_if_fail(fi, 0);

    FAST_UPDATE(!fi->color_loaded,
    {
        fm_file_info_highlight(fi);
        fi->color_loaded = TRUE;
    })

    return fi->color;
}

