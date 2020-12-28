// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

#include "vm.h"
#include "wref.h"

typedef struct {
    WeakRefable base;
    Value *data; // owning reference
    size_t size;
    size_t capacity;
} List;

// Steals (takes move references to):
//   * 'data[0]' ... 'data[ndata - 1]'
UU_INHEADER List *list_new_steal(Value *data, size_t ndata)
{
    List *list = uu_xmalloc(sizeof(List), 1);
    *list = (List) {
        .base = {
            .gc_hdr = {.nrefs = 1, .kind = VK_LIST},
            .wref_first = NULL,
        },
        .data = uu_xmemdup(data, sizeof(Value) * ndata),
        .size = ndata,
        .capacity = ndata,
    };
    return list;
}

UU_INHEADER Value list_pop(List *list)
{
    return list->data[--list->size];
}

// Steals (takes move references to):
//   * 'v'
UU_INHEADER void list_append_steal(List *list, Value v)
{
    if (list->size == list->capacity) {
        list->data = uu_x2realloc(list->data, &list->capacity, sizeof(Value));
    }
    list->data[list->size++] = v;
}

UU_INHEADER void list_destroy(List *list)
{
    wrefable_invalidate(&list->base);
    for (size_t i = 0; i < list->size; ++i)
        value_unref(list->data[i]);
    free(list->data);
    free(list);
}
