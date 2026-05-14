#ifndef MDICT_PARSER_H
#define MDICT_PARSER_H

#include <glib.h>
#include <stdio.h>
#include <stdint.h>

typedef struct _MdictReader MdictReader;

typedef struct {
    char *key;
    uint8_t *data;
    size_t data_len;
} MdictEntry;

MdictReader* mdict_reader_open(const char *path, GError **error);
void         mdict_reader_close(MdictReader *r);

/* Metadata accessors */
const char*  mdict_reader_get_title(MdictReader *r);
const char*  mdict_reader_get_encoding(MdictReader *r);
float        mdict_reader_get_version(MdictReader *r);
uint64_t     mdict_reader_get_num_entries(MdictReader *r);

/* Iterator for entries */
typedef void (*MdictEntryCallback)(const char *key, const uint8_t *data, size_t len, gpointer user_data);
gboolean     mdict_reader_iterate(MdictReader *r, MdictEntryCallback callback, gpointer user_data, GError **error);

#endif /* MDICT_PARSER_H */
