// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "ht.h"
#include "hash.h"
#include "vm.h"
#include "wref.h"
#include "str.h"

typedef struct {
    WeakRefable base;
    Ht keys;

    Value *values;
    size_t values_size;
    size_t values_capacity;
} Dict;

// Steals (takes move references to):
//   * '*kv' ... '*(kv_end - 1)'.
Dict *dict_new_steal(Value *kv, Value *kv_end);

// If no entry with given key, returns 'NULL'.
UU_INHEADER MaybeValue dict_get(Dict *d, String *key)
{
    uint32_t idx = ht_get(
        &d->keys,
        key->data, key->size, key->hash,
        -1);

    if (idx == (uint32_t) -1)
        return NULL;

    Value r = d->values[idx];
    value_ref(r);
    return r;
}

// Returns pointer to the value of the entry with key 'key'.
// If no such entry, inserts new one with nil value.
UU_INHEADER Value *dict_get_ptr(Dict *d, String *key)
{
    uint32_t size = d->values_size;
    uint32_t idx = ht_put(&d->keys, key->data, key->size, key->hash, size);

    if (idx == size) {
        if (d->values_size == d->values_capacity) {
            d->values = uu_x2realloc(d->values, &d->values_capacity, sizeof(Value));
        }
        d->values[d->values_size++] = mk_nil();
    }

    return &d->values[idx];
}

UU_INHEADER bool dict_remove(Dict *d, String *key)
{
    uint32_t idx = ht_remove(&d->keys, key->data, key->size, key->hash, -1);
    if (idx == (uint32_t) -1)
        return false;

    value_unref(d->values[idx]);

    uint32_t last_idx = d->values_size - 1;
    if (idx != last_idx)
        d->values[idx] = d->values[last_idx];
    --d->values_size;
    return true;
}

UU_INHEADER void dict_destroy(Dict *d)
{
    wrefable_invalidate(&d->base);
    ht_destroy(&d->keys);
    for (size_t i = 0; i < d->values_size; ++i)
        value_unref(d->values[i]);
    free(d->values);
    free(d);
}
