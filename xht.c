// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "xht.h"

static __attribute__((noinline, noreturn))
void overflow_handler(void)
{
    UU_PANIC("too many elements in a hash table (would overflow uint32_t)");
}

static void grow_items(xHt *ht)
{
    uint32_t capacity = ht->items_capacity;

    if (UU_UNLIKELY(capacity == UINT32_MAX))
        overflow_handler();

    if (capacity == 0) {
        capacity = 1;
    } else if (UU_UNLIKELY(__builtin_mul_overflow(capacity, 2u, &capacity))) {
        capacity = UINT32_MAX;
    }

    ht->items = uu_xrealloc(ht->items, sizeof(xHt_Item), capacity);
    ht->items_capacity = capacity;
}

static void grow_buckets(xHt *ht)
{
    if (UU_UNLIKELY(__builtin_mul_overflow(ht->nbuckets, 2u, &ht->nbuckets)))
        overflow_handler();
    ht->buckets = uu_xrealloc(ht->buckets, sizeof(uint32_t), ht->nbuckets);
    memset(ht->buckets, '\xFF', sizeof(uint32_t) * (size_t) ht->nbuckets);

    uint32_t mask = ht->nbuckets - 1;
    uint32_t *buckets = ht->buckets;
    uint32_t nitems = ht->items_size;
    xHt_Item *items = ht->items;
    for (uint32_t i = 0; i < nitems; ++i) {
        uint32_t bucket = items[i].hash & mask;
        items[i].next = buckets[bucket];
        buckets[bucket] = i;
    }
}

uint32_t xht_indexed_first(
        xHt *ht,
        uint32_t start_bucket)
{
    uint32_t nbuckets = ht->nbuckets;
    for (uint32_t b = start_bucket; b != nbuckets; ++b) {
        uint32_t i = ht->buckets[b];
        if (i != (uint32_t) -1)
            return i;
    }
    return -1;
}

uint32_t xht_indexed_next(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash)
{
    uint32_t bucket = hash & (ht->nbuckets - 1);

    uint32_t i = ht->buckets[bucket];
    while (i != (uint32_t) -1) {
        xHt_Item item = ht->items[i];
        if (item.nkey == nkey && (nkey == 0 || memcmp(key, item.key, nkey) == 0)) {
            if (item.next != (uint32_t) -1)
                return item.next;
            return xht_indexed_first(ht, bucket + 1);
        }
        i = item.next;
    }
    // no such key
    return -1;
}

xHt_Value *xht_insert_new_unchecked(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        xHt_Value value)
{
    uint32_t idx = ht->items_size++;
    if (idx == ht->items_capacity)
        grow_items(ht);

    uint32_t bucket = hash & (ht->nbuckets - 1);
    ht->items[idx] = (xHt_Item) {
        .key = uu_xmemdup(key, nkey),
        .nkey = nkey,
        .value = value,
        .next = ht->buckets[bucket],
        .hash = hash,
    };
    ht->buckets[bucket] = idx;

    if (((uint64_t) ht->items_size) * 4 > ((uint64_t) ht->nbuckets) * 3)
        grow_buckets(ht);

    return &ht->items[idx].value;
}

static void pop_item_at_index(xHt *ht, uint32_t idx)
{
    xHt_Item *pitem = &ht->items[idx];
    free(pitem->key);

    uint32_t idx_last = ht->items_size - 1;

    if (idx != idx_last) {
        uint32_t bucket = ht->items[idx_last].hash & (ht->nbuckets - 1);
        uint32_t *pi = &ht->buckets[bucket];
        while (*pi != idx_last) {
            pi = &ht->items[*pi].next;
        }
        *pi = idx;
        ht->items[idx] = ht->items[idx_last];
    }

    --ht->items_size;
}

xHt_Value xht_remove(
        xHt *ht,
        const char *key,
        size_t nkey,
        uint32_t hash,
        xHt_Value if_absent)
{
    uint32_t bucket = hash & (ht->nbuckets - 1);

    uint32_t *pi = &ht->buckets[bucket];
    for (;;) {
        uint32_t i = *pi;
        if (i == (uint32_t) -1)
            break;
        xHt_Item *pitem = &ht->items[i];
        if (pitem->nkey == nkey && (nkey == 0 || memcmp(key, pitem->key, nkey) == 0)) {
            xHt_Value value = pitem->value;
            *pi = pitem->next;
            pop_item_at_index(ht, i);
            return value;
        }
        pi = &pitem->next;
    }

    return if_absent;
}
