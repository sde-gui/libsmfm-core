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

FmSymbol * fm_symbol_ref(FmSymbol * symbol)
{
    fm_return_val_if_fail(symbol, NULL);
    g_atomic_int_inc(&symbol->n_ref);
    return symbol;
}

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

const char * fm_symbol_get_cstr(FmSymbol * symbol)
{
    fm_return_val_if_fail(symbol, NULL);
    return symbol->value;
}

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

gboolean fm_symbol_is_equal(FmSymbol * s1, FmSymbol * s2)
{
    return fm_symbol_compare_fast(s1, s2) == 0;
}

