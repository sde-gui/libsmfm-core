/*
 *      fm-symbol.h
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


#ifndef __FM_SYMBOL_H__
#define __FM_SYMBOL_H__

#include <unistd.h>
#include <glib.h>

G_BEGIN_DECLS

#define FM_SYMBOL(symbol)   ((FmSymbol *) symbol)

typedef struct _FmSymbol FmSymbol;

FmSymbol *   fm_symbol_new(const char * value, ssize_t value_size);

FmSymbol *   fm_symbol_ref(FmSymbol * symbol);
void         fm_symbol_unref(FmSymbol * symbol);

const char * fm_symbol_get_cstr(FmSymbol * symbol);

int          fm_symbol_compare(FmSymbol * s1, FmSymbol * s2);
int          fm_symbol_compare_fast(FmSymbol * s1, FmSymbol * s2);

gboolean     fm_symbol_is_equal(FmSymbol * s1, FmSymbol * s2);

void         fm_log_memory_usage_for_symbol(void);

G_END_DECLS

#endif /* __FM_PATH_H__ */
