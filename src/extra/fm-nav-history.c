/*
 *      fm-nav-history.c
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
 * SECTION:fm-nav-history
 * @short_description: Simple navigation history management.
 * @title: FmNavHistory
 *
 * @include: libfm/fm.h
 *
 * The #FmNavHistory object implements history for paths that were
 * entered in some input bar and allows to add, remove or move items in it.
 */

#include "fm-nav-history.h"
#include "fm-utils.h"

struct _FmNavHistory
{
    GObject parent;
    GQueue items;
    GList* cur;
    gint n_max;
    guint n_cur;
    gboolean allow_duplicates;
    gboolean remove_parent;
};

struct _FmNavHistoryClass
{
    GObjectClass parent_class;
};

static void fm_nav_history_finalize (GObject *object);

G_DEFINE_TYPE(FmNavHistory, fm_nav_history, G_TYPE_OBJECT);


static void fm_nav_history_class_init(FmNavHistoryClass *klass)
{
    GObjectClass *g_object_class;

    g_object_class = G_OBJECT_CLASS(klass);
    g_object_class->finalize = fm_nav_history_finalize;
}

static void fm_nav_history_item_free(FmNavHistoryItem* item)
{
    fm_path_unref(item->path);
    g_slice_free(FmNavHistoryItem, item);
}

static void fm_nav_history_finalize(GObject *object)
{
    FmNavHistory *self;

    fm_return_if_fail(object != NULL);
    fm_return_if_fail(FM_IS_NAV_HISTORY(object));

    self = FM_NAV_HISTORY(object);
    g_queue_clear_full(&self->items, (GDestroyNotify) fm_nav_history_item_free);

    G_OBJECT_CLASS(fm_nav_history_parent_class)->finalize(object);
}

static void fm_nav_history_init(FmNavHistory *self)
{
    g_queue_init(&self->items);
    self->n_max = FM_NAV_HISTORY_DEFAULT_SIZE;
    self->allow_duplicates = TRUE;
}

/**
 * fm_nav_history_new
 *
 * Creates a new #FmNavHistory object with empty history.
 *
 * Returns: a new #FmNavHistory object.
 *
 * Since: 0.1.0
 */
FmNavHistory *fm_nav_history_new(void)
{
    return g_object_new(FM_NAV_HISTORY_TYPE, NULL);
}

/**
 * fm_nav_history_list
 * @nh: the history
 *
 * Retrieves full list of the history as #GList of #FmNavHistoryItem.
 * The returned #GList belongs to #FmNavHistory and shouldn't be freed.
 *
 * Returns: (transfer none) (element-type FmNavHistoryItem): full history.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.2: Don't use it in newer code.
 */
const GList* fm_nav_history_list(FmNavHistory* nh)
{
    return nh->items.head;
}

/**
 * fm_nav_history_get_cur
 * @nh: the history
 *
 * Retrieves current selected item of the history. The returned item
 * belongs to #FmNavHistory and shouldn't be freed by caller.
 *
 * Returns: (transfer none): current item.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.2: Use fm_nav_history_get_nth_path() instead.
 */
const FmNavHistoryItem* fm_nav_history_get_cur(FmNavHistory* nh)
{
    return nh->cur ? (FmNavHistoryItem*)nh->cur->data : NULL;
}

/**
 * fm_nav_history_get_cur_link
 * @nh: the history
 *
 * Retrieves current selected item as #GList element containing
 * #FmNavHistoryItem. The returned item belongs to #FmNavHistory and
 * shouldn't be freed by caller.
 *
 * Returns: (transfer none) (element-type FmNavHistoryItem): current item.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.2: Don't use it in newer code.
 */
const GList* fm_nav_history_get_cur_link(FmNavHistory* nh)
{
    return nh->cur;
}

/**
 * fm_nav_history_can_forward
 * @nh: the history
 *
 * Checks if current selected item is the last item in the history.
 *
 * Before 1.0.0 this call had name fm_nav_history_get_can_forward.
 *
 * Returns: %TRUE if cursor can go forward in history.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.2: Use fm_nav_history_get_cur_index() instead.
 */
gboolean fm_nav_history_can_forward(FmNavHistory* nh)
{
    return nh->n_cur > 0;
}

/**
 * fm_nav_history_forward
 * @nh: the history
 * @old_scroll_pos: the scroll position to associate with current item
 *
 * If there is a previous item in the history then sets @old_scroll_pos
 * into current item data and marks previous item current.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.2: Use fm_nav_history_go_to() instead.
 */
void fm_nav_history_forward(FmNavHistory* nh, int old_scroll_pos)
{
    if(nh->cur && nh->cur->prev)
    {
        FmNavHistoryItem* tmp = (FmNavHistoryItem*)nh->cur->data;
        if(tmp) /* remember current scroll pos */
            tmp->scroll_pos = old_scroll_pos;
        nh->cur = nh->cur->prev;
        nh->n_cur--;
    }
}

/**
 * fm_nav_history_can_back
 * @nh: the history
 *
 * Checks if current selected item is the first item in the history.
 *
 * Before 1.0.0 this call had name fm_nav_history_get_can_back.
 *
 * Returns: %TRUE if cursor can go backward in history.
 *
 * Since: 0.1.0
 */
gboolean fm_nav_history_can_back(FmNavHistory* nh)
{
    fm_return_val_if_fail(nh != NULL && FM_IS_NAV_HISTORY(nh), FALSE);

    return nh->cur ? (nh->cur->next != NULL) : FALSE;
}

/**
 * fm_nav_history_back
 * @nh: the history
 * @old_scroll_pos: the scroll position to associate with current item
 *
 * If there is a next item in the history then sets @old_scroll_pos into
 * current item data and marks next item current.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.2: Use fm_nav_history_go_to() instead.
 */
void fm_nav_history_back(FmNavHistory* nh, int old_scroll_pos)
{
    if(nh->cur && nh->cur->next)
    {
        FmNavHistoryItem* tmp = (FmNavHistoryItem*)nh->cur->data;
        if(tmp) /* remember current scroll pos */
            tmp->scroll_pos = old_scroll_pos;
        nh->cur = nh->cur->next;
        nh->n_cur++;
    }
}

static inline void cut_history(FmNavHistory* nh, guint num)
{
    if ((!nh->allow_duplicates || nh->remove_parent) && num > 0)
    {
        FmNavHistoryItem * selected_item = (FmNavHistoryItem *) nh->cur->data;
        const GList * l;
        int index, prev_item_index;
        FmNavHistoryItem * prev_item;
    repeat:
        for (prev_item = NULL, prev_item_index = 0, index = 0, l = fm_nav_history_list(nh); l; index++, l = l->next)
        {
            FmNavHistoryItem * item = (FmNavHistoryItem *) l->data;
            if (selected_item == item)
                continue;

            gboolean remove = (
                (!nh->allow_duplicates && fm_path_equal(selected_item->path, item->path)) ||
                (nh->remove_parent && fm_path_equal(item->path, fm_path_get_parent(selected_item->path))));

            if (remove)
            {
                FmNavHistoryItem* item_for_removal = (FmNavHistoryItem *) g_queue_pop_nth(&nh->items, index);
                fm_nav_history_item_free(item_for_removal);
                goto repeat;
            }

            if (nh->remove_parent && prev_item && fm_path_equal(prev_item->path, fm_path_get_parent(item->path)))
            {
                FmNavHistoryItem* item_for_removal = (FmNavHistoryItem *) g_queue_pop_nth(&nh->items, prev_item_index);
                fm_nav_history_item_free(item_for_removal);
                goto repeat;
            }
            prev_item = item;
            prev_item_index = index;
        }
    }

    while(g_queue_get_length(&nh->items) > num)
    {
        FmNavHistoryItem* item = (FmNavHistoryItem*)g_queue_pop_tail(&nh->items);
        fm_nav_history_item_free(item);
    }
}

/**
 * fm_nav_history_chdir
 * @nh: the history
 * @path: new path to add
 * @old_scroll_pos: the scroll position to associate with current item
 *
 * Sets @old_scroll_pos into current item data and then adds new @path
 * to the beginning of the @nh.
 *
 * Since: 0.1.0
 */
gboolean fm_nav_history_chdir(FmNavHistory* nh, FmPath* path, gint old_scroll_pos)
{
    FmNavHistoryItem* tmp;

    fm_return_val_if_fail(nh && FM_IS_NAV_HISTORY(nh), FALSE);

    /* now we're at the top of the queue. */
    tmp = nh->cur ? (FmNavHistoryItem*)nh->cur->data : NULL;
    if(tmp) /* remember current scroll pos */
        tmp->scroll_pos = old_scroll_pos;

    if( !tmp || !fm_path_equal(tmp->path, path) ) /* we're not chdir to the same path */
    {
        /* if we're not at the top of the queue, remove all items beyond us. */
        while(nh->n_cur > 0)
        {
            tmp = (FmNavHistoryItem*)g_queue_pop_head(&nh->items);
            if(tmp)
                fm_nav_history_item_free(tmp);
            nh->n_cur--;
        }

        /* add a new item */
        tmp = g_slice_new0(FmNavHistoryItem);
        tmp->path = fm_path_ref(path);
        g_queue_push_head(&nh->items, tmp);
        nh->cur = g_queue_peek_head_link(&nh->items);
        cut_history(nh, nh->n_max);

        return TRUE;
    }

    return FALSE;
}

/**
 * fm_nav_history_jump
 * @nh: the history
 * @l: (element-type FmNavHistoryItem): new current item
 * @old_scroll_pos: the scroll position to associate with current item
 *
 * Sets @old_scroll_pos into current item data and then
 * sets current item of @nh to one from @l.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.2: Use fm_nav_history_go_to() instead.
 */
void fm_nav_history_jump(FmNavHistory* nh, GList* l, int old_scroll_pos)
{
    gint n = g_queue_index(&nh->items, l->data);
    if(n >= 0)
        fm_nav_history_go_to(nh, n, old_scroll_pos);
}

/**
 * fm_nav_history_clear
 * @nh: the history
 *
 * Removes all items from the history @nh.
 *
 * Since: 0.1.0
 */
void fm_nav_history_clear(FmNavHistory* nh)
{
    fm_return_if_fail(nh != NULL && FM_IS_NAV_HISTORY(nh));
    g_queue_clear_full(&nh->items, (GDestroyNotify) fm_nav_history_item_free);
    nh->cur = NULL;
    nh->n_cur = 0;
}

/**
 * fm_nav_history_set_max
 * @nh: the history
 * @num: new size of history
 *
 * Sets maximum length of the history @nh to be @num.
 *
 * Since: 0.1.0
 */
void fm_nav_history_set_max(FmNavHistory* nh, guint num)
{
    fm_return_if_fail(nh != NULL && FM_IS_NAV_HISTORY(nh));
    if(num <= nh->n_cur)
    {
        nh->cur = NULL;
        nh->n_cur = 0;
    }
    nh->n_max = num;
    if(num < 1)
        num = 1;
    cut_history(nh, num);
}

/**
 * fm_nav_history_get_cur_index
 * @nh: the history
 *
 * Retrieves index of current item in the history @nh. 0 means current
 * item is at top.
 *
 * Returns: index of current item.
 *
 * Since: 1.0.2
 */
guint fm_nav_history_get_cur_index(FmNavHistory* nh)
{
    fm_return_val_if_fail(nh != NULL && FM_IS_NAV_HISTORY(nh), 0);
    return nh->n_cur;
}

/**
 * fm_nav_history_get_nth_path
 * @nh: the history
 * @n: index of item
 *
 * Retrieves path of the item @n in the history @nh.
 *
 * Returns: path of the item or %NULL if no such item was found.
 *
 * Since: 1.0.2
 */
FmPath* fm_nav_history_get_nth_path(FmNavHistory* nh, guint n)
{
    FmNavHistoryItem *item;

    g_debug("fm_nav_history_get_nth_path %u", n);
    fm_return_val_if_fail(nh != NULL && FM_IS_NAV_HISTORY(nh), NULL);
    if(n == nh->n_cur)
        item = nh->cur->data;
    else
        item = g_queue_peek_nth(&nh->items, n);
    if(item == NULL)
        return NULL;
    return item->path;
}

/**
 * fm_nav_history_go_to
 * @nh: the history
 * @n: new index
 * @old_scroll_pos: scroll position of current folder view
 *
 * Saves the current scroll position into the history. If item with index
 * @n exists in the history then sets it as current item.
 *
 * Returns: path of selected item or %NULL if no such item was found.
 *
 * Since: 1.0.2
 */
FmPath* fm_nav_history_go_to(FmNavHistory* nh, guint n, gint old_scroll_pos)
{
    GList *link;

    fm_return_val_if_fail(nh != NULL && FM_IS_NAV_HISTORY(nh), NULL);
    if(nh->cur)
        ((FmNavHistoryItem*)nh->cur->data)->scroll_pos = old_scroll_pos;
    if(n == nh->n_cur)
        return ((FmNavHistoryItem*)nh->cur->data)->path;
    link = g_queue_peek_nth_link(&nh->items, n);
    if(link == NULL)
        return NULL;
    nh->n_cur = n;
    nh->cur = link;
    return ((FmNavHistoryItem*)link->data)->path;
}

/**
 * fm_nav_history_get_scroll_pos
 * @nh: the history
 *
 * Retrieves saved scroll position for current item.
 *
 * Returns: saved scroll position.
 *
 * Since: 1.0.2
 */
gint fm_nav_history_get_scroll_pos(FmNavHistory* nh)
{
    fm_return_val_if_fail(nh != NULL && FM_IS_NAV_HISTORY(nh), -1);
    return ((FmNavHistoryItem*)nh->cur->data)->scroll_pos;
}

void fm_nav_history_set_allow_duplicates(FmNavHistory* nh, gboolean allow_duplicates)
{
    nh->allow_duplicates = allow_duplicates;
}

gboolean fm_nav_history_get_allow_duplicates(FmNavHistory* nh)
{
    return nh->allow_duplicates;
}

void fm_nav_history_set_remove_parent(FmNavHistory* nh, gboolean remove_parent)
{
    nh->remove_parent = remove_parent;
}

gboolean fm_nav_history_get_remove_parent(FmNavHistory* nh)
{
    return nh->remove_parent;
}
