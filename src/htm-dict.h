#pragma once

#include "zip-mmap.h"
#include "flat-index.h"
#include <glib.h>

typedef struct HtmDict HtmDict;

/* 8-char id for URIs (prefix of SHA1 of zip path). */
const char *htm_dict_id(HtmDict *d);
const char *htm_dict_zip_path(HtmDict *d);
const char *htm_dict_display_name(HtmDict *d);
const char *htm_dict_index_lang(HtmDict *d);
const char *htm_dict_contents_lang(HtmDict *d);
/* Directory prefix inside zip for resources, e.g. "CALD4/" — includes trailing slash. */
const char *htm_dict_resource_prefix(HtmDict *d);

/* Open a .diction dictionary container. Internally it uses the ZIP file format. */
HtmDict *htm_dict_open(const char *zip_path, GError **error);
void htm_dict_close(HtmDict *d);

/* Access to the flat binary index */
FlatIndex *htm_dict_get_flat_index(HtmDict *d);

/* Definition HTML fragment (UTF-8), caller g_free. Empty string if missing. */
char *htm_dict_get_definition_html(HtmDict *d, const char *word);

/* Resource bytes: STORED uses mmap-backed GBytes (no heap copy). Caller g_bytes_unref. */
gboolean htm_dict_read_resource_bytes(HtmDict *d, const char *archive_relative_path, GBytes **out_bytes,
                                      char **out_mime, GError **error);

ZipMmap *htm_dict_peek_zip(HtmDict *d);
