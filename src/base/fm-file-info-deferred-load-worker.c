/*
 *      fm-file-info-deferred-load-worker.c
 *
 *      Copyright (c) 2013 Vadim Ushakov
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fm-config.h"
#include "fm-file-info-deferred-load-worker.h"
#include "fm-utils.h"

gboolean fm_file_info_only_one_ref(FmFileInfo* fi);
gboolean fm_file_info_icon_loaded(FmFileInfo* fi);

static GMutex incomming_list_mutex;
static GSList * incomming_list = NULL;
static GSList * working_list = NULL;

G_LOCK_DEFINE_STATIC(worker_control);
static GThread * worker_thread = NULL;
static gint worker_stop = 0;
static GCond worker_wake_up_condition;

static gpointer worker_thread_func(gpointer data)
{
    const gint64 max_time_slice = G_USEC_PER_SEC * 0.05;
    gint64 time_slice_begin = g_get_monotonic_time();

    long n_items_handled_from_cond_wake_up = 0;
    long n_items_handled_from_timed_wake_up = 0;

    gint stop = g_atomic_int_get(&worker_stop);
    while (!stop)
    {

        g_mutex_lock(&incomming_list_mutex);

        if (!incomming_list && !working_list)
        {
            g_debug("deferred_load_worker: %4ld items handled and incomming list is empty; going to sleep...",
                n_items_handled_from_cond_wake_up);

            fm_log_memory_usage();

            g_cond_wait(&worker_wake_up_condition, &incomming_list_mutex);

            time_slice_begin = g_get_monotonic_time();
            n_items_handled_from_cond_wake_up = 0;
            n_items_handled_from_timed_wake_up = 0;
        }

        if (incomming_list)
        {
            working_list = g_slist_concat(working_list, g_slist_reverse(incomming_list));
            incomming_list = NULL;
        }

        g_mutex_unlock(&incomming_list_mutex);

        while (working_list)
        {
            stop |= g_atomic_int_get(&worker_stop);

            GSList * list_item = working_list;
            working_list = working_list->next;
            FmFileInfo * fi = list_item->data;
            g_slist_free_1(list_item);

            if (fi && !fm_file_info_only_one_ref(fi) && !fm_file_info_icon_loaded(fi) && !stop)
            {
                fm_file_info_get_icon(fi);
                fm_file_info_unref(fi);

                n_items_handled_from_cond_wake_up++;
                n_items_handled_from_timed_wake_up++;

                long long time_slice = g_get_monotonic_time() - time_slice_begin;
                if (time_slice > max_time_slice)
                {
                    g_debug("deferred_load_worker: %4ld items handled in %lld µs",
                        n_items_handled_from_timed_wake_up,
                        time_slice);
                    g_usleep(G_USEC_PER_SEC * 0.1);
                    time_slice_begin = g_get_monotonic_time();
                    n_items_handled_from_timed_wake_up = 0;
                }
            }
            else
            {
                fm_file_info_unref(fi);
            }
        }

        stop |= g_atomic_int_get(&worker_stop);
    }

    //g_print("deferred_load_worker: exit\n");

    return NULL;
}

void fm_file_info_deferred_load_add(FmFileInfo * fi)
{
    //g_print("fm_file_info_deferred_load_add: %s\n", fm_file_info_get_disp_name(fi));
    g_mutex_lock(&incomming_list_mutex);
    incomming_list = g_slist_prepend (incomming_list, fm_file_info_ref(fi));
    g_mutex_unlock(&incomming_list_mutex);
}

void fm_file_info_deferred_load_start(void)
{
    G_LOCK(worker_control);

    g_atomic_int_set(&worker_stop, 0);

    if (!worker_thread)
    {
        worker_thread = g_thread_try_new("fm-file-info-deferred-load-worker", worker_thread_func, NULL, NULL);
    }

    g_cond_broadcast(&worker_wake_up_condition);

    G_UNLOCK(worker_control);
}

void fm_file_info_deferred_load_stop(void)
{
    G_LOCK(worker_control);
    g_atomic_int_set(&worker_stop, 1);

    g_cond_broadcast(&worker_wake_up_condition);

    if (worker_thread)
    {
        g_thread_join(worker_thread);
        worker_thread = NULL;
    }

    G_UNLOCK(worker_control);
}

