#include "dict-cache-builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct DictCacheBuilder {
    char *cache_path;
    FILE *file;
    uint64_t headwords_len;
};

DictCacheBuilder* dict_cache_builder_new(const char *cache_path, uint64_t entry_count) {
    (void)entry_count;
    FILE *f = fopen(cache_path, "wb");
    if (!f) return NULL;

    DictCacheBuilder *b = g_new0(DictCacheBuilder, 1);
    b->cache_path = g_strdup(cache_path);
    b->file = f;
    
    /* Write placeholder for count */
    uint64_t placeholder = 0;
    fwrite(&placeholder, sizeof(uint64_t), 1, f);
    
    return b;
}

void dict_cache_builder_add_headword(DictCacheBuilder *b, const char *word, size_t len, uint64_t *out_off) {
    *out_off = sizeof(uint64_t) + b->headwords_len;
    fwrite(word, 1, len, b->file);
    fwrite("\n", 1, 1, b->file);
    b->headwords_len += (len + 1);
}

void dict_cache_builder_finalize(DictCacheBuilder *b, FlatTreeEntry *entries, uint64_t actual_count) {
    if (!b || !b->file) return;

    /* Write the sorted flat tree entries directly after the headwords */
    fwrite(entries, sizeof(FlatTreeEntry), actual_count, b->file);
    
    /* Rewrite count at the start */
    fseek(b->file, 0, SEEK_SET);
    fwrite(&actual_count, sizeof(uint64_t), 1, b->file);
}

void dict_cache_builder_free(DictCacheBuilder *b) {
    if (b) {
        if (b->file) fclose(b->file);
        g_free(b->cache_path);
        g_free(b);
    }
}
