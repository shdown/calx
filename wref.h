// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "vm.h"

struct WeakRefable;

typedef struct WeakRef {
    GcHeader gc_hdr;
    struct WeakRefable *target;
    struct WeakRef *next;
    struct WeakRef *prev;
} WeakRef;

typedef struct WeakRefable {
    GcHeader gc_hdr;
    WeakRef *wref_first;
} WeakRefable;

UU_INHEADER void wrefable_invalidate(WeakRefable *x)
{
    for (WeakRef *w = x->wref_first; w; w = w->next)
        w->target = NULL;
}

UU_INHEADER WeakRef *wref_new(WeakRefable *target)
{
    WeakRef *old_first = target->wref_first;

    WeakRef *w = uu_xmalloc(sizeof(WeakRef), 1);
    *w = (WeakRef) {
        .gc_hdr = {.nrefs = 1, .kind = VK_WREF},
        .target = target,
        .next = old_first,
        .prev = NULL,
    };

    if (old_first)
        old_first->prev = w;

    target->wref_first = w;
    return w;
}

UU_INHEADER void wref_destroy(WeakRef *w)
{
    WeakRef *prev = w->prev;
    WeakRef *next = w->next;

    if (prev) {
        prev->next = next;
    } else {
        WeakRefable *target = w->target;
        if (target)
            target->wref_first = next;
    }

    if (next)
        next->prev = prev;

    free(w);
}
