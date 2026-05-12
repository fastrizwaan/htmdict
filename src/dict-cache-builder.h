#pragma once

#include "flat-index.h"
#include <stdint.h>

typedef struct DictCacheBuilder DictCacheBuilder;

DictCacheBuilder* dict_cache_builder_new(const char *cache_path, uint64_t entry_count);
void dict_cache_builder_add_headword(DictCacheBuilder *b, const char *word, size_t len, uint64_t *out_off);
void dict_cache_builder_finalize(DictCacheBuilder *b, FlatTreeEntry *entries, uint64_t actual_count);
void dict_cache_builder_free(DictCacheBuilder *b);
