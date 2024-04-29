/*
 *      fm-utils.h
 *
 *      Copyright 2009 - 2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

#include <glib.h>
#include <gio/gio.h>
#include "fm-file-info.h"

#ifndef __FM_UTILS_H__
#define __FM_UTILS_H__

G_BEGIN_DECLS

/**
 * FmAppCommandParseCallback:
 * @opt: key character
 * @user_data: data passed from caller of fm_app_command_parse()
 *
 * The handler which converts key char into string representation.
 *
 * See also: fm_app_command_parse().
 *
 * Returns: string representation.
 *
 * Since: 1.0.0
 */
typedef const char* (*FmAppCommandParseCallback)(char opt, gpointer user_data);

typedef struct _FmAppCommandParseOption FmAppCommandParseOption;

/**
 * FmAppCommandParseOption:
 * @opt: key character
 * @callback: subroutine to get string for substitution
 *
 * Element of correspondence for substitutions by fm_app_command_parse().
 */
struct _FmAppCommandParseOption
{
    char opt;
    FmAppCommandParseCallback callback;
};

int fm_app_command_parse(const char* cmd, const FmAppCommandParseOption* opts,
                         char** ret, gpointer user_data);

char* fm_file_size_to_str(char* buf, size_t buf_size, goffset size, gboolean si_prefix);

gboolean fm_key_file_get_int(GKeyFile* kf, const char* grp, const char* key, int* val);
gboolean fm_key_file_get_bool(GKeyFile* kf, const char* grp, const char* key, gboolean* val);

char* fm_canonicalize_filename(const char* filename, const char* cwd);

char* fm_strdup_replace(char* str, char* old_str, char* new_str);

gboolean fm_run_in_default_main_context(GSourceFunc func, gpointer data);

const char *fm_get_home_dir(void);

GList* fm_get_mime_types_for_file_info_list(FmFileInfoList* files);

void fm_log_memory_usage(void);

#define fm_return_val_if_fail(expr, val)\
do {\
    if (!(expr))\
        return (val);\
} while (0)

#define fm_return_if_fail(expr)\
do {\
    if (!(expr))\
        return;\
} while (0)

G_END_DECLS

#endif
