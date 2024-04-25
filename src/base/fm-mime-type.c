/*
 *      fm-mime-type.c
 *
 *      Copyright 2009 - 2012 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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
 * SECTION:fm-mime-type
 * @short_description: Extended MIME types support.
 * @title: FmMimeType
 *
 * @include: libfm/fm.h
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fm-mime-type.h"

#include <glib/gi18n-lib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

struct _FmMimeType
{
    char* type; /* mime type name */
    char* description;  /* description of the mime type */
    FmIcon* icon;

    /* thumbnailers installed for the mime-type */
    GList* thumbnailers; /* FmMimeType does "not" own the FmThumbnailer objects */

    int n_ref;
};

/* FIXME: how can we handle reload of xdg mime? */

static GHashTable *mime_hash = NULL;
G_LOCK_DEFINE(mime_hash);

/* Preallocated MIME types */
static FmMimeType * inode_directory_type = NULL;
static FmMimeType * inode_chardevice_type = NULL;
static FmMimeType * inode_blockdevice_type = NULL;
static FmMimeType * inode_fifo_type = NULL;
static FmMimeType * inode_symlink_type = NULL;
#ifdef S_ISSOCK
static FmMimeType * inode_socket_type = NULL;
#endif
static FmMimeType * application_x_desktop_type = NULL;
static FmMimeType * mountable_type = NULL;
static FmMimeType * shortcut_type = NULL;


static FmMimeType* fm_mime_type_new(const char* type_name);

void _fm_mime_type_init()
{
    mime_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                       NULL, fm_mime_type_unref);

    /* Preallocated to save hash table lookup. */
    inode_directory_type = fm_mime_type_from_name("inode/directory");
    inode_chardevice_type = fm_mime_type_from_name("inode/chardevice");
    inode_blockdevice_type = fm_mime_type_from_name("inode/blockdevice");
    inode_fifo_type = fm_mime_type_from_name("inode/fifo");
    inode_symlink_type = fm_mime_type_from_name("inode/symlink");
#ifdef S_ISSOCK
    inode_socket_type = fm_mime_type_from_name("inode/socket");
#endif
    application_x_desktop_type = fm_mime_type_from_name("application/x-desktop");

    /* fake mime-types for mountable and shortcuts */
    shortcut_type = fm_mime_type_from_name("inode/x-shortcut");
    shortcut_type->description = g_strdup(_("Shortcuts"));

    mountable_type = fm_mime_type_from_name("inode/x-mountable");
    mountable_type->description = g_strdup(_("Mount Point"));
}

void _fm_mime_type_finalize()
{
    fm_mime_type_unref(inode_directory_type);
    fm_mime_type_unref(inode_chardevice_type);
    fm_mime_type_unref(inode_blockdevice_type);
    fm_mime_type_unref(inode_fifo_type);
    fm_mime_type_unref(inode_symlink_type);
    fm_mime_type_unref(inode_socket_type);
    fm_mime_type_unref(application_x_desktop_type);
    fm_mime_type_unref(mountable_type);
    fm_mime_type_unref(shortcut_type);

    g_hash_table_destroy(mime_hash);
}

/**
 * fm_mime_type_from_file_name
 * @ufile_name: file name to guess
 *
 * Finds #FmMimeType descriptor guessing type from @ufile_name.
 *
 * Before 1.0.0 this API had name fm_mime_type_get_for_file_name.
 *
 * Returns: (transfer full): a #FmMimeType object.
 *
 * Since: 0.1.0
 */
FmMimeType* fm_mime_type_from_file_name(const char* ufile_name)
{
    FmMimeType* mime_type;
    char * type;
    gboolean uncertain;
    type = g_content_type_guess(ufile_name, NULL, 0, &uncertain);
    mime_type = fm_mime_type_from_name(type);
    g_free(type);
    return mime_type;
}

/*****************************************************************************/

/*

shared-mime-info type names for scripts are quite inconsistent:

* sh scripts: `application/x-shellscript`
* ruby scripts: `application/x-ruby`
* python scripts: `text/x-python`
* perl scripts: `application/x-perl` and alias `text/x-perl`
* awk scripts: `application/x-awk`
* fish scripts: `application/x-fishscript` and alias `text/x-fish`
* lua scripts: `text/x-lua`
* tcl scripts: `text/x-tcl`

TODO: Ideally, we should check mime types returned by g_content_type_guess() and make use of the same ones.

TODO: There should be way to disable fast path completely in the settings and always use g_content_type_guess().
*/

#define HAS_PREFIX(buf, prefix) (memcmp(buf, prefix, sizeof(prefix) - 1) == 0)

static
const char * _fast_content_type_guess_script(const char * base_name, const guchar * buf, guint len, struct stat * st)
{
    const char * result = NULL;

    if (buf[0] != '#' || buf[1] != '!')
        return result;
    buf += 2;

    while (*buf == ' ')
        buf++;

    if (HAS_PREFIX(buf, "/bin/sh\n") || HAS_PREFIX(buf, "/bin/bash\n"))
    {
        result = "application/x-shellscript";
    }
    else
    {
        const char * r = NULL;

        int offset = 0;
        if (HAS_PREFIX(buf, "/usr/bin/env "))
            offset = sizeof("/usr/bin/env ") - 1;
        else if (HAS_PREFIX(buf, "/usr/bin/"))
            offset = sizeof("/usr/bin/") - 1;
        else if (HAS_PREFIX(buf, "/bin/"))
            offset = sizeof("/bin/") - 1;

        if (offset)
        {
            if (HAS_PREFIX(buf + offset, "perl"))
            {
                offset += sizeof("perl") - 1;
                r = "text/x-perl";
            }
            else if (HAS_PREFIX(buf + offset, "python"))
            {
                offset += sizeof("python") - 1;
                r = "text/x-python";
            }
            else if (HAS_PREFIX(buf + offset, "ruby"))
            {
                offset += sizeof("ruby") - 1;
                r = "application/x-ruby";
            }
        }

        if (r)
        {

            while (buf[offset] == '.' || isdigit(buf[offset])) {offset++;}

            if (buf[offset] == ' ' || buf[offset] == '\n')
                result = r;
        }
    }

    return result;
}

static
gchar * _fast_content_type_guess(const char * base_name, const guchar * buf, guint len, struct stat * st)
{
    const char * result = NULL;

    if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH) && len > 30)
    {
        result = _fast_content_type_guess_script(base_name, buf, len, st);

        if (!result && (HAS_PREFIX(buf, "\x7F""ELF")))
        {
            if (strcmp(base_name, "core") == 0)
                result = "application/x-core";
            if (strstr(base_name, ".so") == 0)
                result = "application/x-executable";
        }

    }

    return result ? g_strdup(result) : NULL;
}

#undef HAS_PREFIX

static
gchar * _guess_content_for_regular_file(const char* file_path, const char* base_name, struct stat* pstat)
{
    gchar * type1 = NULL;
    gchar * type2 = NULL;

    { /* guess by name */
        gboolean uncertain;
        type1 = g_content_type_guess(base_name, NULL, 0, &uncertain);
        if(!uncertain)
            goto end;
    }

    /* treat an empty file as text/plain  */
    if (pstat->st_size == 0)
    {
        type2 = g_strdup("text/plain");
        goto end;
    }

    int fd = open(file_path, O_RDONLY);
    if(fd >= 0)
    {
        /* #3086703 - PCManFM crashes on non existent directories.
         * http://sourceforge.net/tracker/?func=detail&aid=3086703&group_id=156956&atid=801864
         *
         * NOTE: do not use mmap here. Though we can get little
         * performance gain, this makes our program more vulnerable
         * to I/O errors. If the mapped file is truncated by other
         * processes or I/O errors happen, we may receive SIGBUS.
         * It's a pity that we cannot use mmap for speed up here. */

        char buf[4097];
        ssize_t len = read(fd, buf, MIN(pstat->st_size, 4096));
        buf[len] = 0;

        type2 = _fast_content_type_guess(base_name, (guchar*)buf, len, pstat);
        if (!type2)
            type2 = g_content_type_guess(base_name, (guchar*)buf, len, NULL);

        close(fd);
    }

end:
    if (type2)
    {
        g_free(type1);
        type1 = type2;
    }

    return type1;
}


/*****************************************************************************/

/**
 * fm_mime_type_from_native_file
 * @file_path: full path to file
 * @base_name: file basename
 * @pstat: (allow-none): file atrributes
 *
 * Finds #FmMimeType descriptor for provided data.
 *
 * Before 1.0.0 this API had name fm_mime_type_get_for_native_file.
 *
 * Returns: (transfer full): a #FmMimeType object.
 *
 * Since: 0.1.0
 */
FmMimeType* fm_mime_type_from_native_file(const char* file_path,
                                        const char* base_name,
                                        struct stat* pstat)
{
    struct stat st;

    if(!pstat)
    {
        pstat = &st;
        if(stat(file_path, &st) == -1)
        {
            if(lstat(file_path, &st) == -1)
                return NULL;
        }
    }

    if (S_ISREG(pstat->st_mode))
    {
        gchar * type = _guess_content_for_regular_file(file_path, base_name, pstat);
        FmMimeType * mime_type = fm_mime_type_from_name(type);
        g_free(type);
        return mime_type;
    }

    if (S_ISDIR(pstat->st_mode))
        return fm_mime_type_ref(inode_directory_type);
    if (S_ISCHR(pstat->st_mode))
        return fm_mime_type_ref(inode_chardevice_type);
    if (S_ISBLK(pstat->st_mode))
        return fm_mime_type_ref(inode_blockdevice_type);
    if (S_ISFIFO(pstat->st_mode))
        return fm_mime_type_ref(inode_fifo_type);
    if (S_ISLNK(pstat->st_mode))
        return fm_mime_type_ref(inode_symlink_type);
#ifdef S_ISSOCK
    if (S_ISSOCK(pstat->st_mode))
        return fm_mime_type_ref(inode_socket_type);
#endif
    /* impossible */
    g_debug("Invalid stat mode: %d, %s", pstat->st_mode & S_IFMT, base_name);
    /* FIXME: some files under /proc/self has st_mode = 0, which causes problems.
     *        currently we treat them as files of unknown type. */
    return fm_mime_type_from_name("application/octet-stream");
}

/**
 * fm_mime_type_from_name
 * @type: MIME type name
 *
 * Finds #FmMimeType descriptor for @type.
 *
 * Before 1.0.0 this API had name fm_mime_type_get_for_type.
 *
 * Returns: (transfer full): a #FmMimeType object.
 *
 * Since: 0.1.0
 */
FmMimeType* fm_mime_type_from_name(const char* type)
{
    FmMimeType * mime_type;

    G_LOCK(mime_hash);
    mime_type = g_hash_table_lookup(mime_hash, type);
    if (!mime_type)
    {
        mime_type = fm_mime_type_new(type);
        g_hash_table_insert(mime_hash, mime_type->type, mime_type);
    }
    G_UNLOCK(mime_hash);
    fm_mime_type_ref(mime_type);
    return mime_type;
}

/**
 * fm_mime_type_new
 * @type_name: MIME type name
 *
 * Creates a new #FmMimeType descriptor for @type.
 *
 * Returns: (transfer full): new #FmMimeType object.
 *
 * Since: 0.1.0
 */
FmMimeType* fm_mime_type_new(const char* type_name)
{
    FmMimeType * mime_type = g_slice_new0(FmMimeType);
    mime_type->type = g_strdup(type_name);
    mime_type->n_ref = 1;

    return mime_type;
}

FmMimeType* _fm_mime_type_get_inode_directory()
{
    return inode_directory_type;
}

FmMimeType* _fm_mime_type_get_inode_x_shortcut()
{
    return shortcut_type;
}

FmMimeType* _fm_mime_type_get_inode_x_mountable()
{
    return mountable_type;
}

FmMimeType* _fm_mime_type_get_application_x_desktop()
{
    return application_x_desktop_type;
}

/**
 * fm_mime_type_ref
 * @mime_type: a #FmMimeType descriptor
 *
 * Increments reference count on @mime_type.
 *
 * Returns: @mime_type.
 *
 * Since: 0.1.0
 */
FmMimeType* fm_mime_type_ref(FmMimeType* mime_type)
{
    if (mime_type)
        g_atomic_int_inc(&mime_type->n_ref);
    return mime_type;
}

/**
 * fm_mime_type_unref
 * @mime_type_: a #FmMimeType descriptor
 *
 * Decrements reference count on @mime_type_.
 *
 * Since: 0.1.0
 */
void fm_mime_type_unref(gpointer mime_type_)
{
    if (!mime_type_)
        return;

    FmMimeType* mime_type = (FmMimeType*)mime_type_;
    if (g_atomic_int_dec_and_test(&mime_type->n_ref))
    {
        g_free(mime_type->type);
        g_free(mime_type->description);
        if (mime_type->icon)
            fm_icon_unref(mime_type->icon);
        if(mime_type->thumbnailers)
        {
            /* Note: we do not own references for FmThumbnailer here.
             * Just free the list */
            /* FIXME: this list should be free already or else it's failure
               and fm-thumbnailer.c will try to unref this destroyed object */
            g_list_free(mime_type->thumbnailers);
        }
        g_slice_free(FmMimeType, mime_type);
    }
}

/**
 * fm_mime_type_get_icon
 * @mime_type: a #FmMimeType descriptor
 *
 * Retrieves icon associated with @mime_type. Returned data are owned by
 * @mime_type and should be not freed by caller.
 *
 * Returns: icon.
 *
 * Since: 0.1.0
 */
FmIcon* fm_mime_type_get_icon(FmMimeType* mime_type)
{
    GIcon* gicon;

    if (!mime_type)
        return NULL;

    if (G_UNLIKELY(!mime_type->icon))
    {
        gicon = g_content_type_get_icon(mime_type->type);
        if(strcmp(mime_type->type, "inode/directory") == 0)
            g_themed_icon_prepend_name(G_THEMED_ICON(gicon), "folder");
        else if(g_content_type_can_be_executable(mime_type->type))
            g_themed_icon_append_name(G_THEMED_ICON(gicon), "application-x-executable");

        mime_type->icon = fm_icon_from_gicon(gicon);
        g_object_unref(gicon);
    }

    return mime_type->icon;
}

/**
 * fm_mime_type_get_type
 * @mime_type: a #FmMimeType descriptor
 *
 * Retrieves MIME type name of @mime_type. Returned data are owned by
 * @mime_type and should be not freed by caller.
 *
 * Returns: MIME type name.
 *
 * Since: 0.1.0
 */
const char* fm_mime_type_get_type(FmMimeType* mime_type)
{
    return mime_type ? mime_type->type : NULL;
}

/**
 * fm_mime_type_get_thumbnailers
 * @mime_type: a #FmMimeType descriptor
 *
 * Retrieves list of thumbnailers associated with @mime_type. Returned
 * data are owned by @mime_type and should be not altered by caller.
 *
 * Returns: (element-type gpointer) (transfer none): the list.
 *
 * Since: 1.0.0
 */
const GList* fm_mime_type_get_thumbnailers(FmMimeType* mime_type)
{
    /* FIXME: need this be thread-safe? */
    return mime_type ? mime_type->thumbnailers : NULL;
}

/**
 * fm_mime_type_add_thumbnailer
 * @mime_type: a #FmMimeType descriptor
 * @thumbnailer: anonymous thumbnailer pointer
 *
 * Adds @thumbnailer to list of thumbnailers associated with @mime_type.
 *
 * Since: 1.0.0
 */
void fm_mime_type_add_thumbnailer(FmMimeType* mime_type, gpointer thumbnailer)
{
    /* FIXME: need this be thread-safe? */
    mime_type->thumbnailers = g_list_append(mime_type->thumbnailers, thumbnailer);
}

/**
 * fm_mime_type_remove_thumbnailer
 * @mime_type: a #FmMimeType descriptor
 * @thumbnailer: anonymous thumbnailer pointer
 *
 * Removes @thumbnailer from list of thumbnailers associated with
 * @mime_type.
 *
 * Since: 1.0.0
 */
void fm_mime_type_remove_thumbnailer(FmMimeType* mime_type, gpointer thumbnailer)
{
    /* FIXME: need this be thread-safe? */
    mime_type->thumbnailers = g_list_remove(mime_type->thumbnailers, thumbnailer);
}

/**
 * fm_mime_type_get_desc
 * @mime_type: a #FmMimeType descriptor
 *
 * Retrieves human-readable description of MIME type. Returned data are
 * owned by @mime_type and should be not freed by caller.
 *
 * Returns: MIME type name.
 *
 * Since: 0.1.0
 */
/* Get human-readable description of mime type */
const char* fm_mime_type_get_desc(FmMimeType* mime_type)
{
    /* FIXME: is locking needed here or not? */
    if (G_UNLIKELY(! mime_type->description))
    {
        mime_type->description = g_content_type_get_description(mime_type->type);
        /* FIXME: should handle this better */
        if (G_UNLIKELY(! mime_type->description || ! *mime_type->description))
            mime_type->description = g_content_type_get_description(mime_type->type);
    }
    return mime_type->description;
}
