/*
 *      fm-file-ops-xfer.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fm-file-ops-job-xfer.h"
#include "fm-file-ops-job-delete.h"
#include "fm-utils.h"
#include "fm-path-list.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fm-monitor.h"
#include <glib/gi18n-lib.h>

static const char query[]=
    G_FILE_ATTRIBUTE_STANDARD_TYPE","
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME","
    G_FILE_ATTRIBUTE_STANDARD_NAME","
    G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL","
    G_FILE_ATTRIBUTE_STANDARD_SIZE","
    G_FILE_ATTRIBUTE_UNIX_BLOCKS","
    G_FILE_ATTRIBUTE_UNIX_BLOCK_SIZE","
    G_FILE_ATTRIBUTE_ID_FILESYSTEM;

static void progress_cb(goffset cur, goffset total, gpointer job);

static gboolean _fm_file_ops_job_check_paths(FmFileOpsJob* job, GFile* src, GFileInfo* src_inf, GFile* dest)
{
    GError* err = NULL;
    FmJob* fmjob = FM_JOB(job);
    if(job->type == FM_FILE_OP_MOVE && g_file_equal(src, dest))
    {
        err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED,
            _("Source and destination are the same."));
    }
    else if(g_file_info_get_file_type(src_inf) == G_FILE_TYPE_DIRECTORY
            && g_file_has_prefix(dest, src) )
    {
        const char* msg = NULL;
        if(job->type == FM_FILE_OP_MOVE)
            msg = _("Cannot move a folder into its sub folder");
        else if(job->type == FM_FILE_OP_COPY)
            msg = _("Cannot copy a folder into its sub folder");
        else
            msg = _("Destination is a sub folder of source");
        err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, msg);
    }
    if(err)
    {
        if(!fm_job_is_cancelled(fmjob))
        {
            fm_file_ops_job_emit_cur_file(job, g_file_info_get_display_name(src_inf));
            fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        }
        g_error_free(err);
    }
    return (err == NULL);
}

static gboolean _fm_file_ops_job_copy_file(FmFileOpsJob* job, GFile* src, GFileInfo* inf, GFile* dest)
{
    /* FIXME: prevent copying to self or copying parent dir to child. */
    gboolean ret = FALSE;
    gboolean delete_src = FALSE;
    GError* err = NULL;
    GFileType type;
    guint64 size;
    GFile* new_dest = NULL;
    GFileCopyFlags flags;
    FmJob* fmjob = FM_JOB(job);
    guint32 mode;
    gboolean skip_dir_content = FALSE;

    /* FIXME: g_file_get_child() failed? generate error! */
    fm_return_val_if_fail(dest != NULL, FALSE);

    if( G_LIKELY(inf) )
        g_object_ref(inf);
    else
    {
_retry_query_src_info:
        inf = g_file_query_info(src, query, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, fm_job_get_cancellable(fmjob), &err);
        if( !inf )
        {
            FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
            g_error_free(err);
            err = NULL;
            if(act == FM_JOB_RETRY)
                goto _retry_query_src_info;
            return FALSE;
        }
    }

    if(!_fm_file_ops_job_check_paths(job, src, inf, dest))
    {
        g_object_unref(inf);
        return FALSE;
    }

    /* if this is a cross-device move operation, delete source files. */
    if( job->type == FM_FILE_OP_MOVE )
        delete_src = TRUE;

    /* showing currently processed file. */
    fm_file_ops_job_emit_cur_file(job, g_file_info_get_display_name(inf));

    type = g_file_info_get_file_type(inf);

    size = g_file_info_get_size(inf);
    mode = g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_MODE);

    g_object_unref(inf);
    inf = NULL;

    switch(type)
    {
    case G_FILE_TYPE_DIRECTORY:
        {
            GFileEnumerator* enu;
            gboolean dir_created = FALSE;
_retry_mkdir:
            if( !fm_job_is_cancelled(fmjob) && !job->skip_dir_content &&
                !g_file_make_directory(dest, fm_job_get_cancellable(fmjob), &err) )
            {
                if(err->domain == G_IO_ERROR && err->code == G_IO_ERROR_EXISTS)
                {
                    GFile* dest_cp = new_dest;
                    FmFileOpOption opt = 0;
                    g_error_free(err);
                    err = NULL;

                    new_dest = NULL;
                    opt = fm_file_ops_job_ask_rename(job, src, NULL, dest, &new_dest);
                    if(!new_dest) /* restoring status quo */
                        new_dest = dest_cp;
                    else if(dest_cp) /* we got new new_dest, forget old one */
                        g_object_unref(dest_cp);
                    switch(opt)
                    {
                    case FM_FILE_OP_RENAME:
                        dest = new_dest;
                        goto _retry_mkdir;
                        break;
                    case FM_FILE_OP_SKIP:
                        /* when a dir is skipped, we need to know its total size to calculate correct progress */
                        job->finished += size;
                        fm_file_ops_job_emit_percent(job);
                        job->skip_dir_content = skip_dir_content = TRUE;
                        dir_created = TRUE; /* pretend that dir creation succeeded */
                        break;
                    case FM_FILE_OP_OVERWRITE:
                        dir_created = TRUE; /* pretend that dir creation succeeded */
                        break;
                    case FM_FILE_OP_CANCEL:
                        fm_job_cancel(fmjob);
                        break;
                    case FM_FILE_OP_SKIP_ERROR: ; /* FIXME */
                    }
                }
                else if(!fm_job_is_cancelled(fmjob))
                {
                    FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                    g_error_free(err);
                    err = NULL;
                    if(act == FM_JOB_RETRY)
                        goto _retry_mkdir;
                }
                job->finished += size;
                fm_file_ops_job_emit_percent(job);
            }
            else
            {
                /* chmod the newly created dir properly */
                if(!fm_job_is_cancelled(fmjob) && !job->skip_dir_content)
                {
                    if(mode)
                    {
_retry_chmod_for_dir:
                        mode |= (S_IRUSR|S_IWUSR); /* ensure we have rw permission to this file. */
                        if( !g_file_set_attribute_uint32(dest, G_FILE_ATTRIBUTE_UNIX_MODE,
                                                         mode, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                         fm_job_get_cancellable(fmjob), &err) )
                        {
                            FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                            g_error_free(err);
                            err = NULL;
                            if(act == FM_JOB_RETRY)
                                goto _retry_chmod_for_dir;
                            /* FIXME: some filesystems may not support this. */
                        }
                    }
                    dir_created = TRUE;
                }
                job->finished += size;
                fm_file_ops_job_emit_percent(job);

                if(job->dest_folder_mon)
                    g_file_monitor_emit_event(job->dest_folder_mon, dest, NULL, G_FILE_MONITOR_EVENT_CREATED);
            }

            if(!dir_created) /* if target dir is not created, don't copy dir content */
            {
                if(!job->skip_dir_content)
                    job->skip_dir_content = skip_dir_content = TRUE;
            }

            /* the dest dir is created. let's copy its content. */
            /* FIXME: handle the case when the dir cannot be created. */
            else if(!fm_job_is_cancelled(fmjob))
            {
_retry_enum_children:
                enu = g_file_enumerate_children(src, query,
                                    0, fm_job_get_cancellable(fmjob), &err);
                if(enu)
                {
                    int n_children = 0;
                    int n_copied = 0;
                    ret = TRUE;
                    while( !fm_job_is_cancelled(fmjob) )
                    {
                        inf = g_file_enumerator_next_file(enu, fm_job_get_cancellable(fmjob), &err);
                        if( inf )
                        {
                            ++n_children;
                            /* don't overwrite dir content, only calculate progress. */
                            if(G_UNLIKELY(job->skip_dir_content))
                            {
                                /* FIXME: this is incorrect as we don't do the calculation recursively. */
                                job->finished += g_file_info_get_size(inf);
                                fm_file_ops_job_emit_percent(job);
                            }
                            else
                            {
                                gboolean ret2;
                                GFileMonitor* old_dest_mon = job->dest_folder_mon;
                                GFile* sub = g_file_get_child(src, g_file_info_get_name(inf));
                                GFile* sub_dest = g_file_get_child(dest, g_file_info_get_name(inf));

                                if(g_file_is_native(dest))
                                    job->dest_folder_mon = NULL;
                                else
                                    job->dest_folder_mon = fm_monitor_lookup_dummy_monitor(dest);

                                ret2 = _fm_file_ops_job_copy_file(job, sub, inf, sub_dest);
                                g_object_unref(sub);
                                g_object_unref(sub_dest);

                                if(job->dest_folder_mon)
                                    g_object_unref(job->dest_folder_mon);
                                job->dest_folder_mon = old_dest_mon;

                                if(ret2)
                                    ++n_copied;
                                else
                                    ret = FALSE;
                            }
                            g_object_unref(inf);
                        }
                        else
                        {
                            if(err)
                            {
                                /* FIXME: error handling */
                                fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                                g_error_free(err);
                                err = NULL;
                                /* FM_JOB_RETRY is not supported here */
                                ret = FALSE;
                            }
                            else /* EOF is reached */
                            {
                                /* all files are successfully copied. */
                                if(fm_job_is_cancelled(fmjob))
                                    ret = FALSE;
                                else
                                {
                                    if(dir_created) /* target dir is created */
                                    {
                                        /* some files are not copied */
                                        if(n_children != n_copied)
                                        {
                                            /* if the copy actions are skipped deliberately, it's ok */
                                            if(!job->skip_dir_content)
                                                ret = FALSE;
                                        }
                                    }
                                    /* else job->skip_dir_content is TRUE */
                                }
                                break;
                            }
                        }
                    }
                    g_file_enumerator_close(enu, NULL, &err);
                    g_object_unref(enu);
                }
                else
                {
                    FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                    g_error_free(err);
                    err = NULL;
                    if(act == FM_JOB_RETRY)
                        goto _retry_enum_children;
                }
            }
            if(job->skip_dir_content)
                delete_src = FALSE;
            if(skip_dir_content)
                job->skip_dir_content = FALSE;
        }
        break;

    case G_FILE_TYPE_SPECIAL:
        /* only handle FIFO for local files */
        if(g_file_is_native(src) && g_file_is_native(dest))
        {
            char* src_path = g_file_get_path(src);
            struct stat src_st;
            int lstat_result = lstat(src_path, &src_st);
            g_free(src_path);
            if(lstat_result == 0)
            {
                /* Handle FIFO on native file systems. */
                if(S_ISFIFO(src_st.st_mode))
                {
                    char* dest_path = g_file_get_path(dest);
                    int mkfifo_result = mkfifo(dest_path, src_st.st_mode);
                    g_free(dest_path);
                    if(mkfifo_result == 0)
                    {
                        ret = TRUE;
                        break;
                    }
                    else
                    {
                        /* FIXME: error handling */
                    }
                }
                /* FIXME: how about block device, char device, and socket? */
            }
            else
            {
                /* FIXME: error handling */
            }
        }
        job->finished += size;
        fm_file_ops_job_emit_percent(job);

    default:
        flags = G_FILE_COPY_ALL_METADATA|G_FILE_COPY_NOFOLLOW_SYMLINKS;
_retry_copy:
        if( !g_file_copy(src, dest, flags, fm_job_get_cancellable(fmjob),
                         progress_cb, fmjob, &err) )
        {
            flags &= ~G_FILE_COPY_OVERWRITE;

            /* handle existing files */
            if(err->domain == G_IO_ERROR && err->code == G_IO_ERROR_EXISTS)
            {
                GFile* dest_cp = new_dest;
                FmFileOpOption opt = 0;
                g_error_free(err);
                err = NULL;

                new_dest = NULL;
                opt = fm_file_ops_job_ask_rename(job, src, NULL, dest, &new_dest);
                if(!new_dest) /* restoring status quo */
                    new_dest = dest_cp;
                else if(dest_cp) /* we got new new_dest, forget old one */
                    g_object_unref(dest_cp);
                switch(opt)
                {
                case FM_FILE_OP_RENAME:
                    dest = new_dest;
                    goto _retry_copy;
                    break;
                case FM_FILE_OP_OVERWRITE:
                    flags |= G_FILE_COPY_OVERWRITE;
                    goto _retry_copy;
                    break;
                case FM_FILE_OP_CANCEL:
                    fm_job_cancel(fmjob);
                    break;
                case FM_FILE_OP_SKIP:
                    ret = TRUE;
                    delete_src = FALSE; /* don't delete source file. */
                    break;
                case FM_FILE_OP_SKIP_ERROR: ; /* FIXME */
                }
            }
            else
            {
                gboolean is_no_space = (err->domain == G_IO_ERROR &&
                                        err->code == G_IO_ERROR_NO_SPACE);
                FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                g_error_free(err);
                err = NULL;
                if(act == FM_JOB_RETRY)
                {
                    job->current_file_finished = 0;
                    goto _retry_copy;
                }
                /* FIXME: ask to leave partial content? */
                if(is_no_space)
                    g_file_delete(dest, fm_job_get_cancellable(fmjob), NULL);
                ret = FALSE;
                delete_src = FALSE;
            }
        }
        else
            ret = TRUE;

        job->finished += size;
        job->current_file_finished = 0;

        if( ret && type != G_FILE_TYPE_DIRECTORY )
        {
            if(job->dest_folder_mon)
                g_file_monitor_emit_event(job->dest_folder_mon, dest, NULL, G_FILE_MONITOR_EVENT_CREATED);
        }

        /* update progress */
        fm_file_ops_job_emit_percent(job);
        break;
    }
    /* if this is a cross-device move operation, delete source files. */
    /* ret == TRUE means the copy is successful. */
    if( !fm_job_is_cancelled(fmjob) && ret && delete_src )
        ret = _fm_file_ops_job_delete_file(fmjob, src, inf); /* delete the source file. */

    if(new_dest)
        g_object_unref(new_dest);

    return ret;
}

gboolean _fm_file_ops_job_move_file(FmFileOpsJob* job, GFile* src, GFileInfo* inf, GFile* dest)
{
    /* FIXME: prevent moving to self or moving parent dir to child. */
    GError* err = NULL;
    FmJob* fmjob = FM_JOB(job);
    const char* src_fs_id;
    gboolean ret = TRUE;
    GFile* new_dest = NULL;

    if( G_LIKELY(inf) )
        g_object_ref(inf);
    else
    {
_retry_query_src_info:
        inf = g_file_query_info(src, query, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, fm_job_get_cancellable(fmjob), &err);
        if( !inf )
        {
            FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
            g_error_free(err);
            err = NULL;
            if(act == FM_JOB_RETRY)
                goto _retry_query_src_info;
            return FALSE;
        }
    }

    if(!_fm_file_ops_job_check_paths(job, src, inf, dest))
    {
        g_object_unref(inf);
        return FALSE;
    }

    src_fs_id = g_file_info_get_attribute_string(inf, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
    /* Check if source and destination are on the same device */
    if( job->type == FM_FILE_OP_UNTRASH || g_strcmp0(src_fs_id, job->dest_fs_id) == 0 ) /* same device */
    {
        guint64 size;
        GFileCopyFlags flags = G_FILE_COPY_ALL_METADATA|G_FILE_COPY_NOFOLLOW_SYMLINKS;

        /* showing currently processed file. */
        fm_file_ops_job_emit_cur_file(job, g_file_info_get_display_name(inf));
_retry_move:
        if( !g_file_move(src, dest, flags, fm_job_get_cancellable(fmjob), progress_cb, job, &err))
        {
            flags &= ~G_FILE_COPY_OVERWRITE;
            if(err->domain == G_IO_ERROR && err->code == G_IO_ERROR_EXISTS)
            {
                GFile* dest_cp = new_dest;
                FmFileOpOption opt = 0;

                new_dest = NULL;
                opt = fm_file_ops_job_ask_rename(job, src, NULL, dest, &new_dest);
                if(!new_dest) /* restoring status quo */
                    new_dest = dest_cp;
                else if(dest_cp) /* we got new new_dest, forget old one */
                    g_object_unref(dest_cp);

                g_error_free(err);
                err = NULL;

                switch(opt)
                {
                case FM_FILE_OP_RENAME:
                    dest = new_dest;
                    goto _retry_move;
                    break;
                case FM_FILE_OP_OVERWRITE:
                    flags |= G_FILE_COPY_OVERWRITE;
                    if(g_file_info_get_file_type(inf) == G_FILE_TYPE_DIRECTORY) /* merge dirs */
                    {
                        GFileEnumerator* enu = g_file_enumerate_children(src, query, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                                        fm_job_get_cancellable(fmjob), &err);
                        if(enu)
                        {
                            GFileInfo* child_inf;
                            while(!fm_job_is_cancelled(fmjob))
                            {
                                child_inf = g_file_enumerator_next_file(enu, fm_job_get_cancellable(fmjob), &err);
                                if(child_inf)
                                {
                                    GFile* child = g_file_get_child(src, g_file_info_get_name(child_inf));
                                    GFile* child_dest = g_file_get_child(dest, g_file_info_get_name(child_inf));
                                    _fm_file_ops_job_move_file(job, child, child_inf, child_dest);
                                    g_object_unref(child);
                                    g_object_unref(child_dest);
                                    g_object_unref(child_inf);
                                }
                                else
                                {
                                    if(err) /* error */
                                    {
                                        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                                        g_error_free(err);
                                        err = NULL;
                                    }
                                    else /* EOF */
                                    {
                                        break;
                                    }
                                }
                            }
                            g_object_unref(enu);
                        }
                        else
                        {
                            /*FmJobErrorAction act = */
                            fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                            g_error_free(err);
                            err = NULL;
                            /* if(act == FM_JOB_RETRY)
                                goto _retry_move; */
                        }

                        /* remove source dir after its content is merged with destination dir */
                        if(!g_file_delete(src, fm_job_get_cancellable(fmjob), &err))
                        {
                            fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                            g_error_free(err);
                            err = NULL;
                            /* FIXME: should this be recoverable? */
                        }
                    }
                    else /* the destination is a file, just overwrite it. */
                        goto _retry_move;
                    break;
                case FM_FILE_OP_CANCEL:
                    fm_job_cancel(fmjob);
                    ret = FALSE;
                    break;
                case FM_FILE_OP_SKIP:
                    ret = TRUE;
                    break;
                case FM_FILE_OP_SKIP_ERROR: ; /* FIXME */
                }
            }
            if(err)
            {
                FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
                g_error_free(err);
                err = NULL;
                if(act == FM_JOB_RETRY)
                    goto _retry_move;
            }
        }
        else
        {
            if(job->src_folder_mon)
                g_file_monitor_emit_event(job->src_folder_mon, src, NULL, G_FILE_MONITOR_EVENT_DELETED);
            if(job->dest_folder_mon)
                g_file_monitor_emit_event(job->dest_folder_mon, dest, NULL, G_FILE_MONITOR_EVENT_CREATED);
        }
/*
        size = g_file_info_get_attribute_uint64(inf, G_FILE_ATTRIBUTE_UNIX_BLOCKS);
        size *= g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_BLOCK_SIZE);
*/
        size = g_file_info_get_size(inf);

        job->finished += size;
        fm_file_ops_job_emit_percent(job);
    }
    else /* use copy if they are on different devices */
    {
        /* use copy & delete */
        /* source file will be deleted in _fm_file_ops_job_copy_file() */
        ret = _fm_file_ops_job_copy_file(job, src, inf, dest);
    }

    if(new_dest)
        g_object_unref(new_dest);

    g_object_unref(inf);
    return ret;
}

static void progress_cb(goffset cur, goffset total, gpointer data)
{
    FmFileOpsJob* job = FM_FILE_OPS_JOB(data);
    job->current_file_finished = cur;
    /* update progress */
    fm_file_ops_job_emit_percent(job);
}

gboolean _fm_file_ops_job_copy_run(FmFileOpsJob* job)
{
    gboolean ret = TRUE;
    GFile *dest_dir;
    GFileMonitor *old_mon, *dest_mon;
    GList* l;
    FmJob* fmjob = FM_JOB(job);
    /* prepare the job, count total work needed with FmDeepCountJob */
    FmDeepCountJob* dc = fm_deep_count_job_new(job->srcs, FM_DC_JOB_DEFAULT);
    /* let the deep count job share the same cancellable object. */
    fm_job_set_cancellable(FM_JOB(dc), fm_job_get_cancellable(fmjob));
    /* FIXME: there is no way to cancel the deep count job here. */
    fm_job_run_sync(FM_JOB(dc));
    job->total = dc->total_size;
    if(fm_job_is_cancelled(fmjob))
    {
        g_object_unref(dc);
        return FALSE;
    }
    g_object_unref(dc);
    g_debug("total size to copy: %llu", (long long unsigned int)job->total);

    dest_dir = fm_path_to_gfile(job->dest);
    /* get dummy file monitors for non-native filesystems */
    if( g_file_is_native(dest_dir) )
    {
        old_mon = NULL; /* to satisfy compiler */
        dest_mon = NULL;
    }
    else
    {
        old_mon = job->dest_folder_mon;
        dest_mon = fm_monitor_lookup_dummy_monitor(dest_dir);
        job->dest_folder_mon = dest_mon;
    }

    fm_file_ops_job_emit_prepared(job);

    for(l = fm_path_list_peek_head_link(job->srcs); !fm_job_is_cancelled(fmjob) && l; l=l->next)
    {
        FmPath* path = FM_PATH(l->data);
        GFile* src = fm_path_to_gfile(path);
        GFile* dest = g_file_get_child(dest_dir, fm_path_get_basename(path));

        if(!_fm_file_ops_job_copy_file(job, src, NULL, dest))
            ret = FALSE;
        g_object_unref(src);
        g_object_unref(dest);
    }

    /* g_debug("finished: %llu, total: %llu", job->finished, job->total); */
    fm_file_ops_job_emit_percent(job);

    g_object_unref(dest_dir);
    if(dest_mon)
    {
        g_object_unref(dest_mon);
        job->dest_folder_mon = old_mon;
    }
    return ret;
}

gboolean _fm_file_ops_job_move_run(FmFileOpsJob* job)
{
    GFile *dest_dir;
    GFileMonitor *old_mon, *dest_mon, *old_src_mon;
    GFileInfo* inf;
    GList* l;
    GError* err = NULL;
    FmJob* fmjob = FM_JOB(job);
    dev_t dest_dev = 0;
    gboolean ret = TRUE;
    FmDeepCountJob* dc;

    /* get information of destination folder */
    fm_return_val_if_fail(job->dest, FALSE);
    dest_dir = fm_path_to_gfile(job->dest);
_retry_query_dest_info:
    inf = g_file_query_info(dest_dir, G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL","
                                  G_FILE_ATTRIBUTE_UNIX_DEVICE","
                                  G_FILE_ATTRIBUTE_ID_FILESYSTEM","
                                  G_FILE_ATTRIBUTE_UNIX_DEVICE, 0,
                                  fm_job_get_cancellable(fmjob), &err);
    if(inf)
    {
        job->dest_fs_id = g_intern_string(g_file_info_get_attribute_string(inf, G_FILE_ATTRIBUTE_ID_FILESYSTEM));
        dest_dev = g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_DEVICE); /* needed by deep count */
        g_object_unref(inf);
    }
    else
    {
        FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
        g_error_free(err);
        err = NULL;
        if(act == FM_JOB_RETRY)
            goto _retry_query_dest_info;
        else
        {
            g_object_unref(dest_dir);
            return FALSE;
        }
    }

    /* prepare the job, count total work needed with FmDeepCountJob */
    dc = fm_deep_count_job_new(job->srcs, FM_DC_JOB_PREPARE_MOVE);
    fm_deep_count_job_set_dest(dc, dest_dev, job->dest_fs_id);
    fm_job_run_sync(FM_JOB(dc));
    job->total = dc->total_size;

    if( fm_job_is_cancelled(FM_JOB(dc)) )
    {
        g_object_unref(dest_dir);
        g_object_unref(dc);
        return FALSE;
    }
    g_object_unref(dc);
    g_debug("total size to move: %llu, dest_fs: %s",
            (long long unsigned int)job->total, job->dest_fs_id);

    /* get dummy file monitors for non-native filesystems */
    if( g_file_is_native(dest_dir) )
    {
        old_mon = NULL; /* to satisfy compiler */
        dest_mon = NULL;
    }
    else
    {
        old_mon = job->dest_folder_mon;
        dest_mon = fm_monitor_lookup_dummy_monitor(dest_dir);
        job->dest_folder_mon = dest_mon;
    }

    fm_file_ops_job_emit_prepared(job);

    old_src_mon = job->src_folder_mon;
    for(l = fm_path_list_peek_head_link(job->srcs); !fm_job_is_cancelled(fmjob) && l; l=l->next)
    {
        FmPath* path = FM_PATH(l->data);
        GFile* src = fm_path_to_gfile(path);
        GFile* dest = g_file_get_child(dest_dir, fm_path_get_basename(path));

        /* get dummy file monitors for non-native filesystems */
        job->src_folder_mon = NULL;
        if(!g_file_is_native(src))
        {
            GFile* src_dir = g_file_get_parent(src);
            if(src_dir)
            {
                job->src_folder_mon = fm_monitor_lookup_dummy_monitor(src_dir);
                g_object_unref(src_dir);
            }
        }

        if(!_fm_file_ops_job_move_file(job, src, NULL, dest))
            ret = FALSE;
        g_object_unref(src);
        g_object_unref(dest);

        if(job->src_folder_mon)
            g_object_unref(job->src_folder_mon);

        if(!ret)
            break;
    }
    job->src_folder_mon = old_src_mon;

    g_object_unref(dest_dir);
    if(dest_mon)
    {
        g_object_unref(dest_mon);
        job->dest_folder_mon = old_mon;
    }
    return ret;
}
