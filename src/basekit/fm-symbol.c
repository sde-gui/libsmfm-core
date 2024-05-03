/*
 *      fm-symbol.c
 *
 *      Copyright 2014 Vadim Ushakov <igeekless@gmail.com>
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

#include "fm-symbol.h"
#include "fm-utils.h"

#include <string.h>
#include <limits.h>
#include <glib/gi18n-lib.h>

/**
 * SECTION:fm-symbol
 * @short_description: Immutable reference counted string with optimized comparator.
 * @title: FmSymbol
 *
 * @include: libfm/fm.h
 *
 */

/**
 * FmSymbol:
 *
 * An opaque structure storing a null-terminated immutable string with reference counter.
 */

struct _FmSymbol
{
    gint n_ref;
    int value_size; /* int is intentional in order to reduce memory footprint on 64-bit platforms. */
    char value[1];
};

/*****************************************************************************/

int symbol_total;
int symbol_bytes_total;

void fm_log_memory_usage_for_symbol(void)
{
    g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "memory usage: FmSymbol: %d items, %d KiB",
        g_atomic_int_get(&symbol_total), g_atomic_int_get(&symbol_bytes_total) / 1024);
}

/*****************************************************************************/

/**
 * fm_symbol_new:
 * @value: pointer to a null-terminated string
 * @value_size: length of @value or -1
 *
 * Allocates a new #FmSymbol for the given null-terminated string.
 * The caller may specify the string length, if known. If @value_size is -1,
 * fm_symbol_new() calls strlen() to get the length.
 *
 * Even if the string length is explicitly specified, @value should be a proper
 * null-terminated string and must not contain any null characters except for
 * the terminating one.
 *
 * It is unspecified if fm_symbol_new() returns a pointer to the same #FmSymbol
 * on each invocation with equal @value or not. The current implementation
 * returns a unique #FmSymbol for each invocation, but it may be changed in the future.
 *
 * Returns: (transfer full): a new #FmSymbol struct which needs to be freed with
 * fm_symbol_unref() when it's no more needed.
 *
 * Since: 1.2.0
 */
FmSymbol * fm_symbol_new(const char * value, ssize_t value_size)
{
    if (!value)
        return NULL;

    if (value_size < 0)
        value_size = strlen(value);

    g_atomic_int_inc(&symbol_total);
    g_atomic_int_add(&symbol_bytes_total, sizeof(FmSymbol) + value_size);

    FmSymbol * symbol;
    symbol = (FmSymbol *) g_malloc(sizeof(FmSymbol) + value_size);
    symbol->n_ref = 1;
    symbol->value_size = value_size;
    memcpy(symbol->value, value, value_size);
    symbol->value[value_size] = '\0';

    return symbol;
}

/**
 * fm_symbol_ref:
 * @symbol: (nullable): a #FmSymbol struct
 *
 * Increase reference count of the FmSymbol struct.
 *
 * If @symbol is NULL, the function does nothing.
 *
 * Returns: the FmSymbol struct itself
 *
 * Since: 1.2.0
 */
FmSymbol * fm_symbol_ref(FmSymbol * symbol)
{
    fm_return_val_if_fail(symbol, NULL);
    g_atomic_int_inc(&symbol->n_ref);
    return symbol;
}

/**
 * fm_symbol_unref:
 * @symbol: (nullable): a #FmSymbol struct
 *
 * Decrease reference count of the FmSymbol struct.
 * When the last reference to the struct is released,
 * the struct is freed.
 *
 * If @symbol is NULL, the function does nothing.
 *
 * Since: 1.2.0
 */
void fm_symbol_unref(FmSymbol * symbol)
{
    fm_return_if_fail(symbol);

    if (g_atomic_int_dec_and_test(&symbol->n_ref))
    {
        g_atomic_int_add(&symbol_total, -1);
        g_atomic_int_add(&symbol_bytes_total, -(sizeof(FmSymbol) + symbol->value_size));
        g_free(symbol);
    }
}

/*****************************************************************************/

/**
 * fm_symbol_get_cstr:
 * @symbol: (nullable): a #FmSymbol struct
 *
 * Returns a pointer to null-terminated string stored by #FmSymbol.
 *
 * If @symbol is NULL, the function returns NULL.
 *
 * Returns: (transfer none): a pointer to null-terminated string stored by #FmSymbol.
 *
 * Since: 1.2.0
 */
const char * fm_symbol_get_cstr(FmSymbol * symbol)
{
    fm_return_val_if_fail(symbol, NULL);
    return symbol->value;
}

/**
 * fm_symbol_compare:
 * @s1: (nullable): a #FmSymbol struct
 * @s2: (nullable): a #FmSymbol struct
 *
 * Compares @s1 and @s2 like strcmp().
 * Handles NULL gracefully by sorting it before non-NULL symbols.
 * Comparing two NULL pointers returns 0.
 *
 * Returns: an integer less than, equal to, or greater than zero, if @s1 is <, == or > than @s2.
 *
 * Since: 1.2.0
 */
int fm_symbol_compare(FmSymbol * s1, FmSymbol * s2)
{
    if (s1 == s2)
        return 0;

    if (s1 == NULL)
        return -1;

    if (s2 == NULL)
        return 1;

    return strcmp(s1->value, s2->value);
}

/**
 * fm_symbol_compare_fast:
 * @s1: (nullable): a #FmSymbol struct
 * @s2: (nullable): a #FmSymbol struct
 *
 * Compares @s1 and @s2 in way that is more efficient than fm_symbol_compare().
 * Handles NULL gracefully by sorting it before non-NULL symbols.
 * Comparing two NULL pointers returns 0.
 *
 * This function sorts symbols in an undocumented implementation-defined order
 * so it cannot be used as a strcmp() replacement.
 *
 * Returns: an integer less than, equal to, or greater than zero, if @s1 is <, == or > than @s2.
 *
 * Since: 1.2.0
 */

int fm_symbol_compare_fast(FmSymbol * s1, FmSymbol * s2)
{
    if (s1 == s2)
        return 0;

    if (s1 == NULL)
        return -1;

    if (s2 == NULL)
        return 1;

    if (s1->value_size - s2->value_size)
        return s1->value_size - s2->value_size;

    return memcmp(s1->value, s2->value, s1->value_size);
}

/**
 * fm_symbol_compare:
 * @s1: (nullable): a #FmSymbol struct
 * @s2: (nullable): a #FmSymbol struct
 *
 * Compares two symbols for equality, returning TRUE if they are equal. For use with GHashTable etc.
 *
 * Returns: %TRUE if two symbols are the same length and contain the same bytes
 *
 * Since: 1.2.0
 */
gboolean fm_symbol_is_equal(FmSymbol * s1, FmSymbol * s2)
{
    return (fm_symbol_compare_fast(s1, s2) == 0) ? TRUE : FALSE;
}

