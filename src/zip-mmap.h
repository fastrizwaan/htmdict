#pragma once

#include <glib.h>
#include <stdint.h>
#include <stddef.h>

typedef struct ZipMmap ZipMmap;

typedef struct {
    char *name; /* archive path, POSIX slashes */
    uint64_t local_header_offset;
    uint64_t payload_offset; /* first byte of file data */
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    guint16 method; /* 0 = STORED */
} ZipEntry;

/* mmap entire zip; parse central directory. Read-only. */
ZipMmap *zip_mmap_open(const char *zip_path, GError **error);
void zip_mmap_close(ZipMmap *z);

const char *zip_mmap_data(ZipMmap *z);
size_t zip_mmap_size(ZipMmap *z);

/* Case-insensitive path lookup (slashes normalized, UTF-8 lowercased). */
const ZipEntry *zip_mmap_lookup(ZipMmap *z, const char *archive_path);

GPtrArray *zip_mmap_list_names(ZipMmap *z); /* char* canonical names from archive */

/* STORED: GBytes is static-backed by mmap (valid until zip is closed). DEFLATE: heap-owned. */
gboolean zip_mmap_read_entry_bytes(ZipMmap *z, const ZipEntry *e, GBytes **out, GError **error);

/* Full copy into malloc buffer (any method). Caller g_free's. */
guint8 *zip_mmap_read_entry(ZipMmap *z, const ZipEntry *e, size_t *out_len, GError **error);
