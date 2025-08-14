// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "hash.h"

typedef union {
    void *p;
    uintptr_t u;
} xHt_Value;

typedef HashType_Int xHt_IDX_internal;

typedef struct {
    xHt_IDX_internal v;
} xHt_IDX;

#define XHT_IDX_EQ(X_, Y_) ((X_).v == (Y_).v)
#define XHT_IDX_FROM_INT(I_) ((xHt_IDX) {(I_)})
#define XHT_IDX_BAD  ((xHt_IDX) {-1})

typedef struct {
    char *key;
    size_t nkey;
    xHt_Value value;
    xHt_IDX_internal next;
    HashType hash;
} xHt_Item;

typedef struct {
    xHt_Item *items;
    xHt_IDX_internal *buckets;
    xHt_IDX_internal nbuckets;
    xHt_IDX_internal items_size;
    xHt_IDX_internal items_capacity;
} xHt;

xHt_Value xht_remove(
        xHt *ht,
        const char *key,
        size_t nkey,
        HashType hash,
        xHt_Value if_absent);

#define xht_remove_ptr(ht, key, nkey, hash, if_absent) \
    (xht_remove(ht, key, nkey, hash, (xHt_Value) {.p = (if_absent)}).p)

#define xht_remove_int(ht, key, nkey, hash, if_absent) \
    (xht_remove(ht, key, nkey, hash, (xHt_Value) {.u = (if_absent)}).u)

xHt_Value *xht_insert_new_unchecked(
        xHt *ht,
        const char *key,
        size_t nkey,
        HashType hash,
        xHt_Value value);

#define xht_insert_new_unchecked_ptr(ht, key, nkey, hash, value) \
    (&xht_insert_new_unchecked(ht, key, nkey, hash, (xHt_Value) {.p = (value)})->p)

#define xht_insert_new_unchecked_int(ht, key, nkey, hash, value) \
    (&xht_insert_new_unchecked(ht, key, nkey, hash, (xHt_Value) {.u = (value)})->u)

xHt_IDX xht_indexed_first(
        xHt *ht,
        xHt_IDX start_bucket);

xHt_IDX xht_indexed_next(
        xHt *ht,
        const char *key,
        size_t nkey,
        HashType hash);

UU_INHEADER xHt xht_new(int8_t rank)
{
    xHt_IDX_internal nbuckets = ((xHt_IDX_internal) 1) << rank;
    xHt_IDX_internal *buckets = uu_xmalloc(nbuckets, sizeof(xHt_IDX_internal));
    memset(buckets, '\xFF', sizeof(xHt_IDX_internal) * (size_t) nbuckets);

    return (xHt) {
        .items = NULL,
        .buckets = buckets,
        .nbuckets = nbuckets,
        .items_size = 0,
        .items_capacity = 0,
    };
}

UU_INHEADER size_t xht_size(xHt *ht)
{
    return ht->items_size;
}

UU_INHEADER xHt_Value xht_get(
        xHt *ht,
        const char *key,
        size_t nkey,
        HashType hash,
        xHt_Value if_absent)
{
    xHt_IDX_internal bucket = HASH_TYPE_UNWRAP(hash) & (ht->nbuckets - 1);

    xHt_IDX_internal i = ht->buckets[bucket];
    while (i != (xHt_IDX_internal) -1) {
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
        HashType hash,
        xHt_Value value)
{
    xHt_IDX_internal bucket = HASH_TYPE_UNWRAP(hash) & (ht->nbuckets - 1);

    xHt_IDX_internal i = ht->buckets[bucket];
    while (i != (xHt_IDX_internal) -1) {
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

UU_INHEADER const char *xht_indexed_key(xHt *ht, xHt_IDX idx, size_t *len)
{
    xHt_Item *pitem = &ht->items[idx.v];
    *len = pitem->nkey;
    return pitem->key;
}

UU_INHEADER void xht_destroy(xHt *ht)
{
    free(ht->buckets);
    xHt_Item *items = ht->items;
    size_t nitems = ht->items_size;
    for (size_t i = 0; i < nitems; ++i) {
        free(items[i].key);
    }
    free(items);
}

#define xht_foreach(ht, item, item_end) \
    for (xHt_Item *item = (ht)->items, *item_end = (ht)->items + (ht)->items_size; item != item_end; ++item)
