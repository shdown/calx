// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "xht.h"

#define WRAP(I_) ((xHt_IDX) {(I_)})
#define UNWRAP(X_) ((X_).v)

static __attribute__((noinline, noreturn))
void overflow_handler(bool is_insane_size_within_64_bits)
{
    int nbits = sizeof(xHt_IDX_internal) * 8;
    if (is_insane_size_within_64_bits) {
        nbits -= 2;
    }

    char errmsg[64];
    snprintf(
        errmsg, sizeof(errmsg),
        "hash table: overflow: new size would exceed %d bits",
        nbits);
    UU_PANIC(errmsg);
}

static void grow_items(xHt *ht)
{
    static const xHt_IDX_internal IDX_MAX = (xHt_IDX_internal) -1;

    xHt_IDX_internal capacity = ht->items_capacity;

    if (UU_UNLIKELY(capacity == IDX_MAX))
        overflow_handler(false);

    if (capacity == 0) {
        capacity = 1;
    } else if (UU_UNLIKELY(__builtin_mul_overflow(capacity, 2u, &capacity))) {
        capacity = IDX_MAX;
    }

    ht->items = uu_xrealloc(ht->items, sizeof(xHt_Item), capacity);
    ht->items_capacity = capacity;
}

static void grow_buckets(xHt *ht)
{
    if (UU_UNLIKELY(__builtin_mul_overflow(ht->nbuckets, 2u, &ht->nbuckets)))
        overflow_handler(false);
    ht->buckets = uu_xrealloc(ht->buckets, sizeof(xHt_IDX_internal), ht->nbuckets);
    memset(ht->buckets, '\xFF', sizeof(xHt_IDX_internal) * (size_t) ht->nbuckets);

    xHt_IDX_internal mask = ht->nbuckets - 1;
    xHt_IDX_internal *buckets = ht->buckets;
    xHt_IDX_internal nitems = ht->items_size;

    xHt_Item *items = ht->items;

    for (size_t i = 0; i < nitems; ++i) {
        xHt_IDX_internal bucket = HASH_TYPE_UNWRAP(items[i].hash) & mask;
        items[i].next = buckets[bucket];
        buckets[bucket] = i;
    }
}

xHt_IDX xht_indexed_first(
        xHt *ht,
        xHt_IDX start_bucket)
{
    xHt_IDX_internal nbuckets = ht->nbuckets;
    for (xHt_IDX_internal b = UNWRAP(start_bucket); b != nbuckets; ++b) {
        xHt_IDX_internal i = ht->buckets[b];
        if (i != (xHt_IDX_internal) -1)
            return WRAP(i);
    }
    return WRAP(-1);
}

xHt_IDX xht_indexed_next(
        xHt *ht,
        const char *key,
        size_t nkey,
        HashType hash)
{
    xHt_IDX_internal bucket = HASH_TYPE_UNWRAP(hash) & (ht->nbuckets - 1);

    xHt_IDX_internal i = ht->buckets[bucket];
    while (i != (xHt_IDX_internal) -1) {
        xHt_Item item = ht->items[i];
        if (item.nkey == nkey && (nkey == 0 || memcmp(key, item.key, nkey) == 0)) {
            if (item.next != (xHt_IDX_internal) -1)
                return WRAP(item.next);
            return xht_indexed_first(ht, WRAP(bucket + 1));
        }
        i = item.next;
    }
    // no such key
    return WRAP(-1);
}

xHt_Value *xht_insert_new_unchecked(
        xHt *ht,
        const char *key,
        size_t nkey,
        HashType hash,
        xHt_Value value)
{
    xHt_IDX_internal idx = ht->items_size++;
    if (idx == ht->items_capacity)
        grow_items(ht);

    xHt_IDX_internal bucket = HASH_TYPE_UNWRAP(hash) & (ht->nbuckets - 1);
    ht->items[idx] = (xHt_Item) {
        .key = uu_xmemdup(key, nkey),
        .nkey = nkey,
        .value = value,
        .next = ht->buckets[bucket],
        .hash = hash,
    };
    ht->buckets[bucket] = idx;

    if (sizeof(xHt_IDX_internal) <= 4) {
        if (((uint64_t) ht->items_size) * 4 > ((uint64_t) ht->nbuckets) * 3) {
            grow_buckets(ht);
        }
    } else {
        uint64_t a = ht->items_size;
        uint64_t b = ht->nbuckets;
        if (UU_UNLIKELY(__builtin_mul_overflow(a, 4, &a))) {
            overflow_handler(true);
        }
        if (UU_UNLIKELY(__builtin_mul_overflow(b, 3, &b))) {
            overflow_handler(true);
        }
        if (a > b) {
            grow_buckets(ht);
        }
    }

    return &ht->items[idx].value;
}

static void pop_item_at_index(xHt *ht, xHt_IDX_internal idx)
{
    xHt_Item *pitem = &ht->items[idx];
    free(pitem->key);

    xHt_IDX_internal idx_last = ht->items_size - 1;

    if (idx != idx_last) {
        xHt_IDX_internal bucket = HASH_TYPE_UNWRAP(ht->items[idx_last].hash) & (ht->nbuckets - 1);
        xHt_IDX_internal *pi = &ht->buckets[bucket];
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
        HashType hash,
        xHt_Value if_absent)
{
    xHt_IDX_internal bucket = HASH_TYPE_UNWRAP(hash) & (ht->nbuckets - 1);

    xHt_IDX_internal *pi = &ht->buckets[bucket];
    for (;;) {
        xHt_IDX_internal i = *pi;
        if (i == (xHt_IDX_internal) -1)
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
