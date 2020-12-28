// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "dict.h"

Dict *dict_new_steal(Value *kv, Value *kv_end)
{
    size_t max_size = ((size_t) (kv_end - kv)) / 2;

    Ht keys = ht_new(0);

    Value *values = uu_xmalloc(sizeof(Value), max_size);
    uint32_t nvalues = 0;

    while (kv != kv_end) {
        Value k = *kv++;
        Value v = *kv++;

        String *key = (String *) k;

        uint32_t idx = ht_put(&keys, key->data, key->size, key->hash, nvalues);
        if (idx == nvalues) {
            values[nvalues++] = v;
        } else {
            value_unref(values[idx]);
            values[idx] = v;
        }

        value_unref(k);
    }

    Dict *d = uu_xmalloc(sizeof(Dict), 1);
    *d = (Dict) {
        .base = {
            .gc_hdr = {.nrefs = 1, .kind = VK_DICT},
            .wref_first = NULL,
        },
        .keys = keys,
        .values = values,
        .values_size = nvalues,
        .values_capacity = max_size,
    };
    return d;
}
