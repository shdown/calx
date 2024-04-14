// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "dict.h"

Dict *dict_new_steal(Value *kv, Value *kv_end)
{
    xHt xht = xht_new(0);

    while (kv != kv_end) {
        Value k = *kv++;
        Value v = *kv++;

        String *key = (String *) k;

        MaybeValue *p = (MaybeValue *) xht_put_ptr(&xht, key->data, key->size, key->hash, NULL);
        if (*p) {
            value_unref(*p);
        }
        *p = v;

        value_unref(k);
    }

    Dict *d = uu_xmalloc(sizeof(Dict), 1);
    *d = (Dict) {
        .base = {
            .gc_hdr = {.nrefs = 1, .kind = VK_DICT},
            .wref_first = NULL,
        },
        .xht = xht,
    };
    return d;
}
