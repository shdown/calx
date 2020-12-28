// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

typedef struct {
    char *key;
    size_t nkey;
    uint32_t value;
    uint32_t next;
    uint32_t hash;
} Ht_Item;

typedef struct {
    Ht_Item *items;
    uint32_t *buckets;
    uint32_t nbuckets;
    uint32_t items_size;
    uint32_t items_capacity;
} Ht;

uint32_t ht_remove(
        Ht *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        uint32_t if_absent);

void ht_insert_new_unchecked(
        Ht *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        uint32_t value);

uint32_t ht_indexed_first(
        Ht *ht,
        uint32_t start_bucket);

uint32_t ht_indexed_next(
        Ht *ht,
        const char *key,
        size_t nkey,
        uint32_t hash);

UU_INHEADER Ht ht_new(int8_t rank)
{
    uint32_t nbuckets = ((uint32_t) 1) << rank;
    uint32_t *buckets = uu_xmalloc(nbuckets, sizeof(uint32_t));
    memset(buckets, '\xFF', sizeof(uint32_t) * (size_t) nbuckets);

    return (Ht) {
        .items = NULL,
        .buckets = buckets,
        .nbuckets = nbuckets,
        .items_size = 0,
        .items_capacity = 0,
    };
}

UU_INHEADER uint32_t ht_size(Ht *ht)
{
    return ht->items_size;
}

UU_INHEADER uint32_t ht_get(
        Ht *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        uint32_t if_absent)
{
    uint32_t bucket = hash & (ht->nbuckets - 1);

    uint32_t i = ht->buckets[bucket];
    while (i != (uint32_t) -1) {
        Ht_Item item = ht->items[i];
        if (item.nkey == nkey && (nkey == 0 || memcmp(key, item.key, nkey) == 0))
            return item.value;
        i = item.next;
    }

    return if_absent;
}

UU_INHEADER uint32_t ht_put(
        Ht *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        uint32_t value)
{
    uint32_t bucket = hash & (ht->nbuckets - 1);

    uint32_t i = ht->buckets[bucket];
    while (i != (uint32_t) -1) {
        Ht_Item item = ht->items[i];
        if (item.nkey == nkey && (nkey == 0 || memcmp(key, item.key, nkey) == 0))
            return item.value;
        i = item.next;
    }

    ht_insert_new_unchecked(ht, key, nkey, hash, value);
    return value;
}

UU_INHEADER const char *ht_indexed_key(Ht *ht, uint32_t idx, size_t *len)
{
    Ht_Item *pitem = &ht->items[idx];
    *len = pitem->nkey;
    return pitem->key;
}

UU_INHEADER void ht_destroy(Ht *ht)
{
    free(ht->buckets);
    Ht_Item *items = ht->items;
    uint32_t nitems = ht->items_size;
    for (uint32_t i = 0; i < nitems; ++i) {
        free(items[i].key);
    }
    free(items);
}
