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

#if GLIB_CHECK_VERSION(2,32,0)
#define NEW_THREAD_API 1
#else
#define NEW_THREAD_API 0
#endif

gboolean fm_file_info_only_one_ref(FmFileInfo* fi);
gboolean fm_file_info_icon_loaded(FmFileInfo* fi);

#if NEW_THREAD_API
static GMutex incomming_list_mutex;
#else
static GStaticMutex incomming_list_mutex = G_STATIC_MUTEX_INIT;
#endif
static GSList * incomming_list = NULL;
static GSList * working_list = NULL;

G_LOCK_DEFINE_STATIC(worker_control);
static GThread * worker_thread = NULL;
static gint worker_stop = 0;
#if NEW_THREAD_API
static GCond worker_wake_up_condition;
#else
static GCond * worker_wake_up_condition;
#endif

static gpointer worker_thread_func(gpointer data)
{
    gint stop = g_atomic_int_get(&worker_stop);
    while (!stop)
    {
#if NEW_THREAD_API
        g_mutex_lock(&incomming_list_mutex);
#else
        g_static_mutex_lock(&incomming_list_mutex);
#endif
        if (!incomming_list && !working_list)
        {
            g_print("deferred_load_worker: waiting for wake up\n");
#if NEW_THREAD_API
            g_cond_wait(&worker_wake_up_condition, &incomming_list_mutex);
#else
            g_cond_wait(worker_wake_up_condition, g_static_mutex_get_mutex(&incomming_list_mutex));
#endif
            g_print("deferred_load_worker: wake up\n");
        }

        if (incomming_list)
        {
            working_list = g_slist_concat(working_list, g_slist_reverse(incomming_list));
            incomming_list = NULL;
        }
#if NEW_THREAD_API
        g_mutex_unlock(&incomming_list_mutex);
#else
        g_static_mutex_unlock(&incomming_list_mutex);
#endif

        while (working_list)
        {
            stop |= g_atomic_int_get(&worker_stop);

            GSList * list_item = working_list;
            working_list = working_list->next;
            FmFileInfo * fi = list_item->data;
            g_slist_free_1(list_item);

            g_print("deferred_load_worker: fi %s\n", fm_file_info_get_disp_name(fi));

            if (fi && !fm_file_info_only_one_ref(fi) && !fm_file_info_icon_loaded(fi) && !stop)
            {
                fm_file_info_get_icon(fi);
                g_usleep(10000);
            }
            fm_file_info_unref(fi);
        }

        stop |= g_atomic_int_get(&worker_stop);
    }

    //g_print("deferred_load_worker: exit\n");

    return NULL;
}

void fm_file_info_deferred_load_add(FmFileInfo * fi)
{
    //g_print("fm_file_info_deferred_load_add: %s\n", fm_file_info_get_disp_name(fi));
#if NEW_THREAD_API
    g_mutex_lock(&incomming_list_mutex);
#else
    g_static_mutex_lock(&incomming_list_mutex);
#endif
    incomming_list = g_slist_prepend (incomming_list, fm_file_info_ref(fi));
#if NEW_THREAD_API
    g_mutex_unlock(&incomming_list_mutex);
#else
    g_static_mutex_unlock(&incomming_list_mutex);
#endif
}

void fm_file_info_deferred_load_start(void)
{
    G_LOCK(worker_control);

#if NEW_THREAD_API
#else
    if (!worker_wake_up_condition)
        worker_wake_up_condition = g_cond_new();
#endif

    g_atomic_int_set(&worker_stop, 0);

    if (!worker_thread)
    {
#if NEW_THREAD_API
        worker_thread = g_thread_try_new("fm-file-info-deferred-load-worker", worker_thread_func, NULL, NULL);
#else
        worker_thread = g_thread_create(worker_thread_func, NULL, TRUE, NULL);
#endif
    }

#if NEW_THREAD_API
    g_cond_broadcast(&worker_wake_up_condition);
#else
    g_cond_broadcast(worker_wake_up_condition);
#endif

    G_UNLOCK(worker_control);
}

void fm_file_info_deferred_load_stop(void)
{
    G_LOCK(worker_control);
    g_atomic_int_set(&worker_stop, 1);

#if NEW_THREAD_API
    g_cond_broadcast(&worker_wake_up_condition);
#else
    g_cond_broadcast(worker_wake_up_condition);
#endif

    if (worker_thread)
    {
        g_thread_join(worker_thread);
        worker_thread = NULL;
    }

#if NEW_THREAD_API
#else
    if (worker_wake_up_condition)
    {
        g_cond_free (worker_wake_up_condition);
        worker_wake_up_condition = NULL;
    }
#endif


    G_UNLOCK(worker_control);
}

