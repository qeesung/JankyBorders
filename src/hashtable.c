#include "hashtable.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool table_init(struct table* table, int capacity, table_hash_func hash, table_compare_func cmp) {
  if (!table || capacity <= 0 || !hash || !cmp
      || (size_t)capacity > SIZE_MAX / sizeof(struct bucket*)) {
    return false;
  }

  table->count = 0;
  table->capacity = capacity;
  table->max_load = 0.75f;
  table->hash = hash;
  table->cmp = cmp;
  table->buckets = calloc((size_t)capacity, sizeof(struct bucket*));
  if (!table->buckets) {
    table->capacity = 0;
    return false;
  }
  return true;
}

void table_free(struct table *table) {
  if (!table) return;

  for (int i = 0; i < table->capacity; ++i) {
    struct bucket *next, *bucket = table->buckets[i];
    while (bucket) {
      next = bucket->next;
      free(bucket->key);
      free(bucket);
      bucket = next;
    }
  }

  if (table->buckets) {
    free(table->buckets);
    table->buckets = NULL;
  }
  table->count = 0;
  table->capacity = 0;
}

bool table_clear(struct table* table) {
  if (!table) return false;
  table_hash_func* hash = table->hash;
  table_compare_func* cmp = table->cmp;
  int capacity = table->capacity;

  table_free(table);
  return table_init(table, capacity, hash, cmp);
}

struct bucket** table_get_bucket(struct table* table, void* key) {
  if (!table || !key || !table->buckets || table->capacity <= 0
      || !table->hash || !table->cmp) {
    return NULL;
  }

  struct bucket** bucket = table->buckets
                           + (table->hash(key) % table->capacity);
  while (*bucket) {
    if (table->cmp((*bucket)->key, key)) {
      break;
    }
    bucket = &(*bucket)->next;
  }
  return bucket;
}

static bool table_rehash(struct table *table) {
  if (!table || !table->buckets || table->capacity <= 0
      || table->capacity > INT32_MAX / 2) {
    return false;
  }

  struct bucket **old_buckets = table->buckets;
  int old_capacity = table->capacity;
  int new_capacity = 2 * old_capacity;
  struct bucket** new_buckets = calloc((size_t)new_capacity,
                                       sizeof(struct bucket*));
  if (!new_buckets) return false;

  table->capacity = new_capacity;
  table->buckets = new_buckets;

  for (int i = 0; i < old_capacity; ++i) {
    struct bucket *old_bucket = old_buckets[i];
    while (old_bucket) {
      struct bucket* next_bucket = old_bucket->next;
      struct bucket **new_bucket = table_get_bucket(table, old_bucket->key);
      old_bucket->next = NULL;
      *new_bucket = old_bucket;
      old_bucket = next_bucket;
    }
  }

  free(old_buckets);
  return true;
}

bool _table_add(struct table* table, void* key, size_t key_size, void* value) {
  if (!key || key_size == 0) return false;
  struct bucket** bucket = table_get_bucket(table, key);
  if (!bucket) return false;

  if (*bucket) {
    if (!(*bucket)->value) {
      (*bucket)->value = value;
    }
    return true;
  } else {
    if (table->count == INT32_MAX) return false;
    struct bucket* new_bucket = malloc(sizeof(struct bucket));
    if (!new_bucket) return false;
    new_bucket->key = malloc(key_size);
    if (!new_bucket->key) {
      free(new_bucket);
      return false;
    }
    new_bucket->value = value;
    memcpy(new_bucket->key, key, key_size);
    new_bucket->next = NULL;
    *bucket = new_bucket;
    ++table->count;

    float load = (1.0f * table->count) / table->capacity;
    if (load > table->max_load) {
      table_rehash(table);
    }
    return true;
  }
}

void table_remove(struct table* table, void* key) {
  struct bucket **bucket = table_get_bucket(table, key);
  if (!bucket) return;

  struct bucket* next;
  if (*bucket) {
    free((*bucket)->key);
    next = (*bucket)->next;
    free(*bucket);
    *bucket = next;
    --table->count;
  }
}

void* table_find(struct table* table, void* key) {
  struct bucket** bucket_ref = table_get_bucket(table, key);
  if (!bucket_ref) return NULL;
  struct bucket* bucket = *bucket_ref;
  return bucket ? bucket->value : NULL;
}
