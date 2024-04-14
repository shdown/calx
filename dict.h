// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "xht.h"
#include "hash.h"
#include "vm.h"
#include "wref.h"
#include "str.h"

typedef struct {
    WeakRefable base;
    xHt xht;
} Dict;

// Steals (takes move references to):
//   * '*kv' ... '*(kv_end - 1)'.
Dict *dict_new_steal(Value *kv, Value *kv_end);

// If no entry with given key, returns 'NULL'.
UU_INHEADER MaybeValue dict_get(Dict *d, String *key)
{
    MaybeValue r = xht_get_ptr(&d->xht, key->data, key->size, key->hash, NULL);
    if (r) {
        value_ref(r);
    }
    return r;
}

// Returns pointer to the value of the entry with key 'key'.
// If no such entry, inserts new one with nil value.
UU_INHEADER Value *dict_get_ptr(Dict *d, String *key)
{
    MaybeValue *r = (MaybeValue *) xht_put_ptr(&d->xht, key->data, key->size, key->hash, NULL);
    if (!*r) {
        *r = mk_nil();
    }
    return r;
}

UU_INHEADER bool dict_remove(Dict *d, String *key)
{
    MaybeValue r = xht_remove_ptr(&d->xht, key->data, key->size, key->hash, NULL);
    if (!r) {
        return false;
    }
    value_unref(r);
    return true;
}

UU_INHEADER void dict_destroy(Dict *d)
{
    wrefable_invalidate(&d->base);
    xht_foreach(&d->xht, item, item_end) {
        value_unref(item->value.p);
    }
    xht_destroy(&d->xht);
    free(d);
}
