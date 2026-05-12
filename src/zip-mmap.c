#include "zip-mmap.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_Z64_EOCD_SIG 0x06064b50u
#define ZIP_Z64_LOC_SIG 0x07064b50u
#define ZIP_CENTRAL_SIG 0x02014b50u
#define ZIP_LOCAL_SIG 0x04034b50u
#define ZIP64_EXTRA_ID 0x0001u

struct ZipMmap {
    int fd;
    uint8_t *data;
    size_t size;
    GHashTable *by_name; /* lowercase key -> ZipEntry* */
    GPtrArray *all_names;
};

static void zip_entry_destroy(gpointer p) {
    ZipEntry *e = p;
    if (!e)
        return;
    g_free(e->name);
    g_free(e);
}

static gboolean read_u16(const uint8_t *p, size_t off, size_t max, guint16 *out) {
    if (off + 2 > max)
        return FALSE;
    *out = (guint16)p[off] | ((guint16)p[off + 1] << 8);
    return TRUE;
}

static gboolean read_u32(const uint8_t *p, size_t off, size_t max, guint32 *out) {
    if (off + 4 > max)
        return FALSE;
    *out = (guint32)p[off] | ((guint32)p[off + 1] << 8) | ((guint32)p[off + 2] << 16) | ((guint32)p[off + 3] << 24);
    return TRUE;
}

static gboolean read_u64(const uint8_t *p, size_t off, size_t max, uint64_t *out) {
    if (off + 8 > max)
        return FALSE;
    *out = (uint64_t)p[off] | ((uint64_t)p[off + 1] << 8) | ((uint64_t)p[off + 2] << 16) | ((uint64_t)p[off + 3] << 24) |
           ((uint64_t)p[off + 4] << 32) | ((uint64_t)p[off + 5] << 40) | ((uint64_t)p[off + 6] << 48) | ((uint64_t)p[off + 7] << 56);
    return TRUE;
}

static gboolean find_eocd(const uint8_t *p, size_t n, size_t *eocd_off) {
    if (n < 22)
        return FALSE;
    size_t i = n - 22;
    size_t guard = n > 65536 + 22 ? n - (65536 + 22) : 0;
    for (; i >= guard; i--) {
        guint32 sig;
        if (!read_u32(p, i, n, &sig))
            break;
        if (sig == ZIP_EOCD_SIG) {
            *eocd_off = i;
            return TRUE;
        }
        if (i == 0)
            break;
    }
    return FALSE;
}

/* Raw archive name: POSIX slashes only (preserve case for ZipEntry->name). */
static char *normalize_zip_name(const char *name, gsize len) {
    char *s = g_strndup(name, len);
    for (char *q = s; *q; q++) {
        if (*q == '\\')
            *q = '/';
    }
    return s;
}

static char *normalize_lookup_path(const char *path) {
    if (!path)
        return NULL;
    while (*path == '/' || *path == '\\')
        path++;
    if (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
        path += 2;
    char *s = g_strdup(path);
    for (char *q = s; *q; q++) {
        if (*q == '\\')
            *q = '/';
    }
    return s;
}

/* Parse Zip64 extended information in central directory extra (header 0x0001). */
static void apply_zip64_central_extra(const uint8_t *ex, guint16 ex_len, guint32 u32, guint32 c32, guint32 l32, guint16 disk16,
                                      uint64_t *u64, uint64_t *c64, uint64_t *l64, guint32 *disk32_out) {
    gboolean need_u = (u32 == G_MAXUINT32);
    gboolean need_c = (c32 == G_MAXUINT32);
    gboolean need_l = (l32 == G_MAXUINT32);
    gboolean need_d = (disk16 == 0xffffu);
    *u64 = u32;
    *c64 = c32;
    *l64 = l32;
    *disk32_out = disk16;
    size_t i = 0;
    while (i + 4 <= ex_len) {
        guint16 hid, hlen;
        if (!read_u16(ex, i, ex_len, &hid) || !read_u16(ex, i + 2, ex_len, &hlen))
            break;
        if (i + 4u + (size_t)hlen > ex_len)
            break;
        if (hid == ZIP64_EXTRA_ID) {
            size_t o = i + 4;
            if (need_u) {
                if (o + 8 <= i + 4u + hlen && read_u64(ex, o, ex_len, u64))
                    o += 8;
            }
            if (need_c) {
                if (o + 8 <= i + 4u + hlen && read_u64(ex, o, ex_len, c64))
                    o += 8;
            }
            if (need_l) {
                if (o + 8 <= i + 4u + hlen && read_u64(ex, o, ex_len, l64))
                    o += 8;
            }
            if (need_d && o + 4 <= i + 4u + hlen) {
                guint32 d;
                if (read_u32(ex, o, ex_len, &d))
                    *disk32_out = d;
            }
            break;
        }
        i += 4u + (size_t)hlen;
    }
}

static gboolean parse_local_payload_offset(const uint8_t *p, size_t n, uint64_t local_off, uint64_t *payload_off,
                                           GError **error) {
    if (local_off + 30 > n) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "truncated local header");
        return FALSE;
    }
    guint32 sig = 0;
    if (!read_u32(p, (size_t)local_off, n, &sig) || sig != ZIP_LOCAL_SIG) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "bad local signature");
        return FALSE;
    }
    guint16 fn_len = 0, ex_len = 0;
    read_u16(p, (size_t)local_off + 26, n, &fn_len);
    read_u16(p, (size_t)local_off + 28, n, &ex_len);
    *payload_off = local_off + 30 + fn_len + ex_len;
    if (*payload_off > n) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "local header overflow");
        return FALSE;
    }
    return TRUE;
}

/* If Zip64 EOCD locator sits immediately before classic EOCD, fill 64-bit CD bounds. */
static gboolean try_zip64_eocd(const uint8_t *p, size_t n, size_t eocd, guint64 *cd_off, guint64 *cd_size, guint64 *total_entries,
                                 GError **error) {
    (void)error;
    if (eocd < 20)
        return FALSE;
    guint32 loc_sig = 0;
    if (!read_u32(p, eocd - 20, n, &loc_sig) || loc_sig != ZIP_Z64_LOC_SIG)
        return FALSE;
    guint64 z64_off = 0;
    if (!read_u64(p, eocd - 12, n, &z64_off) || z64_off + 56 > n)
        return FALSE;
    guint32 sig = 0;
    if (!read_u32(p, (size_t)z64_off, n, &sig) || sig != ZIP_Z64_EOCD_SIG)
        return FALSE;
    /* zip64 eocd: +32 total entries (8), +40 cd size (8), +48 cd offset (8) */
    if (!read_u64(p, (size_t)z64_off + 32, n, total_entries))
        return FALSE;
    if (!read_u64(p, (size_t)z64_off + 40, n, cd_size))
        return FALSE;
    if (!read_u64(p, (size_t)z64_off + 48, n, cd_off))
        return FALSE;
    return TRUE;
}

ZipMmap *zip_mmap_open(const char *zip_path, GError **error) {
    int fd = open(zip_path, O_RDONLY);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "open: %s", g_strerror(errno));
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "fstat: %s", g_strerror(errno));
        close(fd);
        return NULL;
    }
    if (st.st_size == 0 || (size_t)st.st_size != (size_t)st.st_size) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "empty or huge zip");
        close(fd);
        return NULL;
    }
    size_t n = (size_t)st.st_size;
    void *m = mmap(NULL, n, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "mmap: %s", g_strerror(errno));
        close(fd);
        return NULL;
    }

    ZipMmap *z = g_new0(ZipMmap, 1);
    z->fd = fd;
    z->data = (uint8_t *)m;
    z->size = n;
    z->by_name = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, zip_entry_destroy);
    z->all_names = g_ptr_array_new_with_free_func(g_free);

    size_t eocd = 0;
    if (!find_eocd(z->data, n, &eocd)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "EOCD not found");
        zip_mmap_close(z);
        return NULL;
    }

    guint16 total16 = 0, entries_on_disk16 = 0;
    guint32 cd_size32 = 0, cd_off32 = 0;
    if (!read_u16(z->data, eocd + 8, n, &entries_on_disk16) || !read_u16(z->data, eocd + 10, n, &total16) ||
        !read_u32(z->data, eocd + 12, n, &cd_size32) || !read_u32(z->data, eocd + 16, n, &cd_off32)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "bad EOCD");
        zip_mmap_close(z);
        return NULL;
    }

    guint64 cd_off = cd_off32;
    guint64 cd_size = cd_size32;
    guint64 total_entries = total16;

    guint64 z64_cd_off = 0, z64_cd_size = 0, z64_total = 0;
    if (try_zip64_eocd(z->data, n, eocd, &z64_cd_off, &z64_cd_size, &z64_total, error)) {
        if (total16 == 0xffffu)
            total_entries = z64_total;
        if (cd_size32 == G_MAXUINT32)
            cd_size = z64_cd_size;
        if (cd_off32 == G_MAXUINT32)
            cd_off = z64_cd_off;
    } else if (total16 == 0xffffu || cd_off32 == G_MAXUINT32 || cd_size32 == G_MAXUINT32) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "zip64 EOCD required but not found");
        zip_mmap_close(z);
        return NULL;
    }

    (void)entries_on_disk16;

    if (cd_off > n || cd_size > n || cd_off + cd_size > n) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "central directory out of range");
        zip_mmap_close(z);
        return NULL;
    }

    size_t pos = (size_t)cd_off;
    const size_t cd_end = (size_t)(cd_off + cd_size);

    for (guint64 ei = 0; ei < total_entries; ei++) {
        if (pos + 46 > cd_end || pos + 46 > n) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "truncated central directory");
            zip_mmap_close(z);
            return NULL;
        }
        guint32 sig = 0;
        if (!read_u32(z->data, pos, n, &sig) || sig != ZIP_CENTRAL_SIG) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "bad central signature");
            zip_mmap_close(z);
            return NULL;
        }
        guint16 method = 0, fn_len = 0, ex_len = 0, comment_len = 0, disk16 = 0;
        read_u16(z->data, pos + 10, n, &method);
        read_u16(z->data, pos + 28, n, &fn_len);
        read_u16(z->data, pos + 30, n, &ex_len);
        read_u16(z->data, pos + 32, n, &comment_len);
        read_u16(z->data, pos + 34, n, &disk16);
        guint32 comp32 = 0, uncomp32 = 0, local32 = 0;
        read_u32(z->data, pos + 20, n, &comp32);
        read_u32(z->data, pos + 24, n, &uncomp32);
        read_u32(z->data, pos + 42, n, &local32);
        if (pos + 46u + (size_t)fn_len + ex_len + comment_len > cd_end || pos + 46u + (size_t)fn_len + ex_len + comment_len > n) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "central record overflow");
            zip_mmap_close(z);
            return NULL;
        }

        uint64_t uncomp64 = uncomp32, comp64 = comp32, local64 = local32;
        guint32 disk32 = disk16;
        if (ex_len > 0) {
            apply_zip64_central_extra(z->data + pos + 46 + fn_len, ex_len, uncomp32, comp32, local32, disk16, &uncomp64, &comp64,
                                      &local64, &disk32);
            (void)disk32;
        }

        char *name = normalize_zip_name((const char *)(z->data + pos + 46), fn_len);
        char *key = g_utf8_strdown(name, -1);

        ZipEntry *ent = g_new(ZipEntry, 1);
        ent->name = name;
        ent->local_header_offset = local64;
        ent->compressed_size = comp64;
        ent->uncompressed_size = uncomp64;
        ent->method = method;
        if (!parse_local_payload_offset(z->data, n, ent->local_header_offset, &ent->payload_offset, error)) {
            g_free(key);
            g_free(ent->name);
            g_free(ent);
            zip_mmap_close(z);
            return NULL;
        }
        if (ent->payload_offset + ent->compressed_size > n) {
            g_free(key);
            g_free(ent->name);
            g_free(ent);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "entry data past EOF");
            zip_mmap_close(z);
            return NULL;
        }

        g_hash_table_insert(z->by_name, key, ent);
        g_ptr_array_add(z->all_names, g_strdup(name));

        pos += 46u + (size_t)fn_len + ex_len + comment_len;
    }

    return z;
}

void zip_mmap_close(ZipMmap *z) {
    if (!z)
        return;
    if (z->data && z->size)
        munmap(z->data, z->size);
    if (z->fd >= 0)
        close(z->fd);
    if (z->by_name)
        g_hash_table_destroy(z->by_name);
    if (z->all_names)
        g_ptr_array_free(z->all_names, TRUE);
    g_free(z);
}

const char *zip_mmap_data(ZipMmap *z) {
    return z ? (const char *)z->data : NULL;
}

size_t zip_mmap_size(ZipMmap *z) {
    return z ? z->size : 0;
}

const ZipEntry *zip_mmap_lookup(ZipMmap *z, const char *archive_path) {
    if (!z || !archive_path)
        return NULL;
    char *norm = normalize_lookup_path(archive_path);
    if (!norm)
        return NULL;
    char *key = g_utf8_strdown(norm, -1);
    g_free(norm);
    const ZipEntry *e = g_hash_table_lookup(z->by_name, key);
    g_free(key);
    return e;
}

GPtrArray *zip_mmap_list_names(ZipMmap *z) {
    return z ? z->all_names : NULL;
}

gboolean zip_mmap_read_entry_bytes(ZipMmap *z, const ZipEntry *e, GBytes **out, GError **error) {
    if (!z || !e || !out) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "bad args");
        return FALSE;
    }
    const uint8_t *src = z->data + e->payload_offset;
    if (e->method == 0) {
        if (e->compressed_size != e->uncompressed_size) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "STORED size mismatch");
            return FALSE;
        }
        if (e->uncompressed_size > (uint64_t)G_MAXSIZE || (size_t)e->uncompressed_size > z->size) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "STORED entry too large");
            return FALSE;
        }
        if (e->payload_offset + e->compressed_size > z->size) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "STORED range past EOF");
            return FALSE;
        }
        *out = g_bytes_new_static(src, (gsize)e->uncompressed_size);
        return TRUE;
    }
    if (e->method != 8) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "unsupported compression %u", e->method);
        return FALSE;
    }
    if (e->uncompressed_size > (uint64_t)G_MAXUINT32) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "DEFLATE entry too large for zlib");
        return FALSE;
    }
    uLongf dest_len = (uLongf)e->uncompressed_size;
    guint8 *dest = g_malloc(dest_len ? dest_len : 1);
    
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.next_in = (Bytef *)src;
    strm.avail_in = (uInt)e->compressed_size;
    strm.next_out = dest;
    strm.avail_out = (uInt)dest_len;

    int zr = inflateInit2(&strm, -MAX_WBITS);
    if (zr == Z_OK) {
        zr = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        if (zr == Z_STREAM_END) {
            dest_len = strm.total_out;
            zr = Z_OK;
        } else if (zr == Z_OK) {
            zr = Z_BUF_ERROR;
        }
    }

    if (zr != Z_OK) {
        g_free(dest);
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "inflate failed (%d)", zr);
        return FALSE;
    }
    *out = g_bytes_new_take(dest, (gsize)dest_len);
    return TRUE;
}

guint8 *zip_mmap_read_entry(ZipMmap *z, const ZipEntry *e, size_t *out_len, GError **error) {
    GBytes *gb = NULL;
    if (!zip_mmap_read_entry_bytes(z, e, &gb, error))
        return NULL;
    gsize sz = 0;
    const void *data = g_bytes_get_data(gb, &sz);
    guint8 *copy = g_memdup2(data, sz);
    g_bytes_unref(gb);
    if (out_len)
        *out_len = sz;
    return copy;
}
