// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

typedef union {
    void *p;
    uintptr_t u;
} xHt_Value;

typedef struct {
    char *key;
    size_t nkey;
    xHt_Value value;
    uint32_t next;
    uint32_t hash;
} xHt_Item;

typedef struct {
    xHt_Item *items;
    uint32_t *buckets;
    uint32_t nbuckets;
    uint32_t items_size;
    uint32_t items_capacity;
} xHt;

xHt_Value xht_remove(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        xHt_Value if_absent);

#define xht_remove_ptr(ht, key, nkey, hash, if_absent) \
    (xht_remove(ht, key, nkey, hash, (xHt_Value) {.p = (if_absent)}).p)

#define xht_remove_int(ht, key, nkey, hash, if_absent) \
    (xht_remove(ht, key, nkey, hash, (xHt_Value) {.u = (if_absent)}).u)

xHt_Value *xht_insert_new_unchecked(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        xHt_Value value);

#define xht_insert_new_unchecked_ptr(ht, key, nkey, hash, value) \
    (&xht_insert_new_unchecked(ht, key, nkey, hash, (xHt_Value) {.p = (value)})->p)

#define xht_insert_new_unchecked_int(ht, key, nkey, hash, value) \
    (&xht_insert_new_unchecked(ht, key, nkey, hash, (xHt_Value) {.u = (value)})->u)

uint32_t xht_indexed_first(
        xHt *ht,
        uint32_t start_bucket);

uint32_t xht_indexed_next(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash);

UU_INHEADER xHt xht_new(int8_t rank)
{
    uint32_t nbuckets = ((uint32_t) 1) << rank;
    uint32_t *buckets = uu_xmalloc(nbuckets, sizeof(uint32_t));
    memset(buckets, '\xFF', sizeof(uint32_t) * (size_t) nbuckets);

    return (xHt) {
        .items = NULL,
        .buckets = buckets,
        .nbuckets = nbuckets,
        .items_size = 0,
        .items_capacity = 0,
    };
}

UU_INHEADER uint32_t xht_size(xHt *ht)
{
    return ht->items_size;
}

UU_INHEADER xHt_Value xht_get(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        xHt_Value if_absent)
{
    uint32_t bucket = hash & (ht->nbuckets - 1);

    uint32_t i = ht->buckets[bucket];
    while (i != (uint32_t) -1) {
        xHt_Item item = ht->items[i];
        if (item.nkey == nkey && (nkey == 0 || memcmp(key, item.key, nkey) == 0))
            return item.value;
        i = item.next;
    }

    return if_absent;
}

#define xht_get_ptr(ht, key, nkey, hash, if_absent) \
    (xht_get(ht, key, nkey, hash, (xHt_Value) {.p = (if_absent)}).p)

#define xht_get_int(ht, key, nkey, hash, if_absent) \
    (xht_get(ht, key, nkey, hash, (xHt_Value) {.u = (if_absent)}).u)

UU_INHEADER xHt_Value *xht_put(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        xHt_Value value)
{
    uint32_t bucket = hash & (ht->nbuckets - 1);

    uint32_t i = ht->buckets[bucket];
    while (i != (uint32_t) -1) {
        xHt_Item item = ht->items[i];
        if (item.nkey == nkey && (nkey == 0 || memcmp(key, item.key, nkey) == 0))
            return &ht->items[i].value;
        i = item.next;
    }

    return xht_insert_new_unchecked(ht, key, nkey, hash, value);
}

#define xht_put_ptr(ht, key, nkey, hash, value) \
    (&xht_put(ht, key, nkey, hash, (xHt_Value) {.p = (value)})->p)

#define xht_put_int(ht, key, nkey, hash, value) \
    (&xht_put(ht, key, nkey, hash, (xHt_Value) {.u = (value)})->u)

UU_INHEADER const char *xht_indexed_key(xHt *ht, uint32_t idx, size_t *len)
{
    xHt_Item *pitem = &ht->items[idx];
    *len = pitem->nkey;
    return pitem->key;
}

UU_INHEADER void xht_destroy(xHt *ht)
{
    free(ht->buckets);
    xHt_Item *items = ht->items;
    uint32_t nitems = ht->items_size;
    for (uint32_t i = 0; i < nitems; ++i) {
        free(items[i].key);
    }
    free(items);
}

#define xht_foreach(ht, item, item_end) \
    for (xHt_Item *item = (ht)->items, *item_end = (ht)->items + (ht)->items_size; item != item_end; ++item)
