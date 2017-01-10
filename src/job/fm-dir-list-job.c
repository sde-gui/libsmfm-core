/*
 *      fm-dir-list-job.c
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
 * SECTION:fm-dir-list-job
 * @short_description: Job to get listing of directory.
 * @title: FmDirListJob
 *
 * @include: libfm/fm.h
 *
 * The #FmDirListJob can be used to gather list of #FmFileInfo that some
 * directory contains.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _BSD_SOURCE

#include <dirent.h>
#include <errno.h>
#include <string.h>

#include "fm-dir-list-job.h"
#include "fm-file-info-job.h"
#include "fm-mime-type.h"
#include "fm-file-info.h"
#include "fm-utils.h"
#include "glib-compat.h"

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

enum {
    FILES_FOUND,
    N_SIGNALS
};

static void fm_dir_list_job_dispose              (GObject *object);
G_DEFINE_TYPE(FmDirListJob, fm_dir_list_job, FM_TYPE_JOB);

static int signals[N_SIGNALS];

static gboolean fm_dir_list_job_run(FmJob *job);
static void fm_dir_list_job_finished(FmJob* job);

static gboolean emit_found_files(gpointer user_data);

static void fm_dir_list_job_class_init(FmDirListJobClass *klass)
{
    GObjectClass *g_object_class;
    FmJobClass* job_class = FM_JOB_CLASS(klass);
    g_object_class = G_OBJECT_CLASS(klass);
    g_object_class->dispose = fm_dir_list_job_dispose;
    /* use finalize from parent class */

    job_class->run = fm_dir_list_job_run;
    job_class->finished = fm_dir_list_job_finished;

    /**
     * FmDirListJob::files-found
     * @job: a job that emitted the signal
     * @files: (element-type FmFileInfo): #GSList of found files
     *
     * The #FmDirListJob::files-found signal is emitted for every file
     * found during directory listing. By default the signal is not
     * emitted for performance reason. This can be turned on by calling
     * fm_dir_list_job_set_incremental().
     *
     * Since: 1.0.2
     */
    signals[FILES_FOUND] =
        g_signal_new("files-found",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(FmDirListJobClass, files_found),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
                     G_TYPE_NONE, 1, G_TYPE_POINTER);

}


static void fm_dir_list_job_init(FmDirListJob *job)
{
    job->files = fm_file_info_list_new();
    fm_job_init_cancellable(FM_JOB(job));
}

/**
 * fm_dir_list_job_new
 * @path: path to directory to get listing
 * @dir_only: %TRUE to include only directories in the list
 *
 * Creates a new #FmDirListJob for directory listing. If @dir_only is
 * %TRUE then objects other than directories will be omitted from the
 * listing.
 *
 * Returns: (transfer full): a new #FmDirListJob object.
 *
 * Since: 0.1.0
 */
FmDirListJob* fm_dir_list_job_new(FmPath* path, gboolean dir_only)
{
    FmDirListJob* job = (FmDirListJob*)g_object_new(FM_TYPE_DIR_LIST_JOB, NULL);
    job->dir_path = fm_path_ref(path);
    job->dir_only = dir_only;
    return job;
}

/**
 * fm_dir_list_job_new_for_gfile
 * @gf: descriptor of directory to get listing
 *
 * Creates a new #FmDirListJob for listing of directory @gf.
 *
 * Returns: (transfer full): a new #FmDirListJob object.
 *
 * Since: 0.1.0
 */
FmDirListJob* fm_dir_list_job_new_for_gfile(GFile* gf)
{
    /* FIXME: should we cache this with hash table? Or, the cache
     * should be done at the level of FmFolder instead? */
    FmDirListJob* job = (FmDirListJob*)g_object_new(FM_TYPE_DIR_LIST_JOB, NULL);
    job->dir_path = fm_path_new_for_gfile(gf);
    return job;
}

static void fm_dir_list_job_dispose(GObject *object)
{
    FmDirListJob *job;

    fm_return_if_fail(object != NULL);
    fm_return_if_fail(FM_IS_DIR_LIST_JOB(object));

    job = (FmDirListJob*)object;

    if(job->dir_path)
    {
        fm_path_unref(job->dir_path);
        job->dir_path = NULL;
    }

    if(job->dir_fi)
    {
        fm_file_info_unref(job->dir_fi);
        job->dir_fi = NULL;
    }

    if(job->files)
    {
        fm_file_info_list_unref(job->files);
        job->files = NULL;
    }

    if(job->delay_add_files_handler)
    {
        g_source_remove(job->delay_add_files_handler);
        job->delay_add_files_handler = 0;
        g_slist_free_full(job->files_to_add, (GDestroyNotify)fm_file_info_unref);
        job->files_to_add = NULL;
    }

    if (G_OBJECT_CLASS(fm_dir_list_job_parent_class)->dispose)
        (* G_OBJECT_CLASS(fm_dir_list_job_parent_class)->dispose)(object);
}

static DIR * opendir_with_error(const char * path, GError ** error)
{
    DIR * dir = opendir(path);
    if (!dir)
    {
        gint saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    _("Error opening directory '%s': %s"), path, g_strerror(saved_errno));
    }

    return dir;
}

#if defined _DIRENT_HAVE_D_TYPE || defined HAVE_STRUCT_DIRENT_D_TYPE
#define DIRENT_MIGHT_BE_SYMLINK(d) \
    ((d)->d_type == DT_UNKNOWN || (d)->d_type == DT_LNK)
#define DIRENT_MIGHT_BE_DIR(d)	 \
    ((d)->d_type == DT_DIR || DIRENT_MIGHT_BE_SYMLINK (d))
#else
#define DIRENT_MIGHT_BE_SYMLINK(d) 1
#define DIRENT_MIGHT_BE_DIR(d)     1
#endif

static gboolean fm_dir_list_job_run_posix(FmDirListJob* job)
{
    FmJob* fmjob = FM_JOB(job);
    FmFileInfo* fi;
    GError *err = NULL;
    char* path_str;
    DIR * dir = NULL;

    long item_count = 0;
    long item_count_step = 0;
    long long start_time = g_get_monotonic_time();

    path_str = fm_path_to_str(job->dir_path);

    fi = fm_file_info_new_from_path_unfilled(job->dir_path);
    if( _fm_file_info_job_get_info_for_native_file(fmjob, fi, path_str, NULL) )
    {
        if(! fm_file_info_is_directory(fi))
        {
            err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("The specified directory is not valid"));
            fm_file_info_unref(fi);
            fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
            g_error_free(err);
            g_free(path_str);
            return FALSE;
        }
        job->dir_fi = fi;
    }
    else
    {
        err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("The specified directory is not valid"));
        fm_file_info_unref(fi);
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        g_error_free(err);
        g_free(path_str);
        return FALSE;
    }

    dir = opendir_with_error(path_str, &err);
    if (dir)
    {
        struct dirent * entry;
        GString* fpath = g_string_sized_new(4096);
        int dir_len = strlen(path_str);
        g_string_append_len(fpath, path_str, dir_len);
        if(fpath->str[dir_len-1] != '/')
        {
            g_string_append_c(fpath, '/');
            ++dir_len;
        }
        while ( !fm_job_is_cancelled(fmjob) && (entry = readdir(dir)) )
        {
            const char* name = entry->d_name;

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;

            FmPath* new_path;
            g_string_truncate(fpath, dir_len);
            g_string_append(fpath, name);

            if (job->dir_only) /* if we only want directories */
            {
                if (!DIRENT_MIGHT_BE_DIR(entry))
                    continue;

                struct stat st;
                /* FIXME: this results in an additional stat() call, which is inefficient */
                if(stat(fpath->str, &st) == -1 || !S_ISDIR(st.st_mode))
                    continue;
            }

            new_path = fm_path_new_child(job->dir_path, name);
            fi = fm_file_info_new_from_path_unfilled(new_path);
            fm_path_unref(new_path);

        _retry:
            if( _fm_file_info_job_get_info_for_native_file(fmjob, fi, fpath->str, &err) )
                fm_dir_list_job_add_found_file(job, fi);
            else /* failed! */
            {
                FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MILD);
                g_error_free(err);
                err = NULL;
                if(act == FM_JOB_RETRY)
                    goto _retry;
            }
            fm_file_info_unref(fi);

            item_count++;
            item_count_step++;
            long long interval = g_get_monotonic_time() - start_time;
            if (interval > G_USEC_PER_SEC * 0.25)
            {
                start_time += interval;
                const char * format = ngettext(
                    "reading folder listing... (%ld items read)",
                    "reading folder listing... (%ld items read)", item_count);
                fm_job_report_status(fmjob, format, item_count);
                g_debug("FmDirListJob: %s:  items read: %ld + %ld = %ld",
                    fm_file_info_get_name(job->dir_fi),
                    item_count - item_count_step,
                    item_count_step,
                    item_count);
                item_count_step = 0;
            }

        }
        g_string_free(fpath, TRUE);
        closedir(dir);

        const char * format = ngettext(
            "%ld items read",
            "%ld items read", item_count);
        fm_job_report_status(fmjob, format, item_count);
    }
    else
    {
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        g_error_free(err);
    }
    g_free(path_str);
    return TRUE;
}

static gboolean fm_dir_list_job_run_gio(FmDirListJob* job)
{
    GError * err = NULL;
    FmJob * fmjob = FM_JOB(job);

    const char * query;
    if (job->dir_only)
    {
        query = G_FILE_ATTRIBUTE_STANDARD_TYPE","G_FILE_ATTRIBUTE_STANDARD_NAME","
                G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN","G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP","
                G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK","G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL","
                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME","G_FILE_ATTRIBUTE_STANDARD_ICON","
                G_FILE_ATTRIBUTE_STANDARD_SIZE","G_FILE_ATTRIBUTE_STANDARD_TARGET_URI","
                "unix::*,time::*,access::*,id::filesystem";
    }
    else
    {
        query = gfile_info_query_attribs;
    }

    GFile * dir_gf = NULL;
    GFileInfo * dir_ginfo = NULL;
    GFileEnumerator * dir_enumerator = NULL;
    GFileInfo * child_ginfo = NULL;

    #define G_ERROR_FREE(object) do { \
        if (object)\
        {\
            g_error_free(object);\
            object = NULL;\
        }\
    } while (0)

    #define UNREF(object) do { \
        if (object)\
        {\
            g_object_unref(object);\
            object = NULL;\
        }\
    } while (0)

    #define CLEANUP() do { \
        UNREF(dir_gf);\
        UNREF(dir_ginfo);\
        UNREF(dir_enumerator);\
        UNREF(child_ginfo);\
    } while (0)

_retry: ;

    long item_count = 0;
    long long start_time = g_get_monotonic_time();

    CLEANUP();

    dir_gf = fm_path_to_gfile(job->dir_path);

    dir_ginfo = g_file_query_info(dir_gf, gfile_info_query_attribs, 0, fm_job_get_cancellable(fmjob), &err);
    if (!dir_ginfo)
    {
        FmJobErrorAction action = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
        G_ERROR_FREE(err);
        if (action == FM_JOB_RETRY)
            goto _retry;
        else
            goto do_abort;
    }

    if (g_file_info_get_file_type(dir_ginfo) != G_FILE_TYPE_DIRECTORY)
    {
        err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("The specified directory is not valid"));
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        G_ERROR_FREE(err);
        goto do_abort;
    }

    job->dir_fi = fm_file_info_new_from_gfileinfo(job->dir_path, dir_ginfo);

    dir_enumerator = g_file_enumerate_children(dir_gf, query, 0, fm_job_get_cancellable(fmjob), &err);

    if (!dir_enumerator)
    {
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        G_ERROR_FREE(err);
        goto do_abort;
    }

    while (!fm_job_is_cancelled(fmjob))
    {
        child_ginfo = g_file_enumerator_next_file(dir_enumerator, fm_job_get_cancellable(fmjob), &err);

        if (child_ginfo)
        {
            if (job->dir_only)
            {
                /* FIXME: handle symlinks */
                if (g_file_info_get_file_type(child_ginfo) != G_FILE_TYPE_DIRECTORY)
                {
                    UNREF(child_ginfo);
                    continue;
                }
            }

            /* virtual folders may return childs not within them */
            FmPath * dir = fm_path_new_for_gfile(g_file_enumerator_get_container(dir_enumerator));
            FmPath * sub = fm_path_new_child(dir, g_file_info_get_name(child_ginfo));
            FmFileInfo * fi = fm_file_info_new_from_gfileinfo(sub, child_ginfo);
            fm_dir_list_job_add_found_file(job, fi);
            fm_file_info_unref(fi);
            fm_path_unref(sub);
            fm_path_unref(dir);
        }
        else
        {
            if (!err)
                break;

            FmJobErrorAction action = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MILD);
            G_ERROR_FREE(err);
            if (action != FM_JOB_CONTINUE) /* FIXME: retry not supported */
                goto do_abort;
        }
        UNREF(child_ginfo);

        item_count++;
        long long interval = g_get_monotonic_time() - start_time;
        if (interval > G_USEC_PER_SEC * 0.25)
        {
            start_time += interval;
            const char * format = ngettext(
                "reading folder listing... (%ld items read)",
                "reading folder listing... (%ld items read)", item_count);
            fm_job_report_status(fmjob, format, item_count);
            g_debug("FmDirListJob: %s: %ld items read", fm_file_info_get_name(job->dir_fi), item_count);
        }

    }

    CLEANUP();
    return TRUE;

do_abort:

    CLEANUP();
    return FALSE;

    #undef CLEANUP
    #undef UNREF
    #undef G_ERROR_FREE
}

static gboolean fm_dir_list_job_run(FmJob* fmjob)
{
    gboolean ret;
    FmDirListJob* job = FM_DIR_LIST_JOB(fmjob);
    fm_return_val_if_fail(job->dir_path != NULL, FALSE);
    if(fm_path_is_native(job->dir_path)) /* if this is a native file on real file system */
        ret = fm_dir_list_job_run_posix(job);
    else /* this is a virtual path or remote file system path */
        ret = fm_dir_list_job_run_gio(job);
    return ret;
}

static void fm_dir_list_job_finished(FmJob* job)
{
    FmDirListJob* dirlist_job = FM_DIR_LIST_JOB(job);
    FmJobClass* job_class = FM_JOB_CLASS(fm_dir_list_job_parent_class);

    if(dirlist_job->emit_files_found)
    {
        if(dirlist_job->delay_add_files_handler)
        {
            g_source_remove(dirlist_job->delay_add_files_handler);
            emit_found_files(dirlist_job);
        }
    }
    if(job_class->finished)
        job_class->finished(job);
}

#if 0
/**
 * fm_dir_list_job_get_dir_path
 * @job: the job that collected listing
 *
 * Retrieves the path of the directory being listed.
 *
 * Returns: (transfer none): FmPath for the directory.
 *
 * Since: 1.0.2
 */
FmPath* fm_dir_list_job_get_dir_path(FmDirListJob* job)
{
    return job->dir_path;
}

/**
 * fm_dir_list_job_get_dir_info
 * @job: the job that collected listing
 *
 * Retrieves the information of the directory being listed.
 *
 * Returns: (transfer none): FmFileInfo for the directory.
 *
 * Since: 1.0.2
 */
FmFileInfo* fm_dir_list_job_get_dir_info(FmDirListJob* job)
{
    return job->dir_fi;
}
#endif

/**
 * fm_dir_list_job_get_files
 * @job: the job that collected listing
 *
 * Retrieves gathered listing from the @job. This function may be called
 * only from #FmJob::finished signal handler. Returned data is owned by
 * the @job and should be not freed by caller.
 *
 *
 * Returns: (transfer none): list of gathered data.
 *
 * Since: 0.1.0
 */
FmFileInfoList* fm_dir_list_job_get_files(FmDirListJob* job)
{
    return job->files;
}

#if 0
void fm_dir_list_job_set_emit_files_found(FmDirListJob* job, gboolean emit_files_found)
{
    job->emit_files_found = emit_files_found;
}

gboolean fm_dir_list_job_get_emit_files_found(FmDirListJob* job)
{
    return job->emit_files_found;
}
#endif

static gboolean emit_found_files(gpointer user_data)
{
    /* this callback is called from the main thread */
    FmDirListJob* job = FM_DIR_LIST_JOB(user_data);
    /* g_print("emit_found_files: %d\n", g_slist_length(job->files_to_add)); */

    if(g_source_is_destroyed(g_main_current_source()))
        return FALSE;
    g_signal_emit(job, signals[FILES_FOUND], 0, job->files_to_add);
    g_slist_free_full(job->files_to_add, (GDestroyNotify)fm_file_info_unref);
    job->files_to_add = NULL;
    job->delay_add_files_handler = 0;
    return FALSE;
}

static gpointer queue_add_file(FmJob* fmjob, gpointer user_data)
{
    FmDirListJob* job = FM_DIR_LIST_JOB(fmjob);
    FmFileInfo* file = FM_FILE_INFO(user_data);
    /* this callback is called from the main thread */
    /* g_print("queue_add_file: %s\n", fm_file_info_get_disp_name(file)); */
    job->files_to_add = g_slist_prepend(job->files_to_add, fm_file_info_ref(file));
    if(job->delay_add_files_handler == 0)
        job->delay_add_files_handler = g_timeout_add_seconds_full(G_PRIORITY_LOW,
                        1, emit_found_files, g_object_ref(job), g_object_unref);
    return NULL;
}

/**
 * fm_dir_list_job_add_found_file
 * @job: the job that collected listing
 * @file: a FmFileInfo of the newly found file
 *
 * This API may be called by the classes derived of FmDirListJob only.
 * Application developers should not use this API.
 * When a new file is found in the dir being listed, implementations
 * of FmDirListJob should call this API with the info of the newly found
 * file. The FmFileInfo will be added to the found file list.
 * 
 * If emission of the #FmDirListJob::files-found signal is turned on by
 * fm_dir_list_job_set_incremental(), the signal will be emitted
 * for the newly found files after several new files are added.
 * See the document for the signal for more detail.
 *
 * Since: 1.0.2
 */
void fm_dir_list_job_add_found_file(FmDirListJob* job, FmFileInfo* file)
{
    fm_file_info_list_push_tail(job->files, file);
    if(G_UNLIKELY(job->emit_files_found))
        fm_job_call_main_thread(FM_JOB(job), queue_add_file, file);
}

#if 0
/**
 * fm_dir_list_job_set_dir_path
 * @job: the job that collected listing
 * @path: a FmPath of the directory being loaded.
 *
 * This API is called by the implementation of FmDirListJob only.
 * Application developers should not use this API most of the time.
 *
 * Since: 1.0.2
 */
void fm_dir_list_job_set_dir_path(FmDirListJob* job, FmPath* path)
{
    if(job->dir_path)
        fm_path_unref(job->dir_path);
    job->dir_path = fm_path_ref(path);
}

/**
 * fm_dir_list_job_set_dir_info
 * @job: the job that collected listing
 * @info: a FmFileInfo of the directory being loaded.
 *
 * This API is called by the implementation of FmDirListJob only.
 * Application developers should not use this API most of the time.
 *
 * Since: 1.0.2
 */
void fm_dir_list_job_set_dir_info(FmDirListJob* job, FmFileInfo* info)
{
    if(job->dir_fi)
        fm_file_info_unref(job->dir_fi);
    job->dir_fi = fm_file_info_ref(info);
}
#endif

/**
 * fm_dir_list_job_set_incremental
 * @job: the job descriptor
 * @set: %TRUE if job should send the #FmDirListJob::files-found signal
 *
 * Sets whether @job should send the #FmDirListJob::files-found signal
 * on found files before the @job is finished or not.
 * This should only be called before the @job is launched.
 *
 * Since: 1.0.2
 */
void fm_dir_list_job_set_incremental(FmDirListJob* job, gboolean set)
{
    job->emit_files_found = set;
}
