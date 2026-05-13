#include "htm-dict.h"
#include "dict-cache.h"
#include "dict-cache-builder.h"
#include "dict-fts-index.h"
#include <glib/gstdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* ────────────────────────────────────────────────────────────── */
/*  Private types                                                  */
/* ────────────────────────────────────────────────────────────── */

typedef struct {
    char   *word;
    guint64 off;
    guint32 len;
} TmpEntry;

struct HtmDict {
    char *zip_path;
    char *id;
    char *display_name;
    char *index_lang;
    char *contents_lang;
    char *html_entry;
    char *resource_prefix;

    ZipMmap         *zip;
    const uint8_t   *html_base;
    size_t           html_size;
    gboolean         html_is_sidecar;
    int              sidecar_fd;
    uint8_t         *sidecar_map;
    size_t           sidecar_size;

    /* Flat binary prefix index (mmap'd) */
    FlatIndex   *flat_index;
    GMappedFile *flat_mf;

    char *sidecar_path;
    char *flat_path;   /* kept so we can delete on rebuild */
};

/* ────────────────────────────────────────────────────────────── */
/*  Public accessors                                               */
/* ────────────────────────────────────────────────────────────── */

const char *htm_dict_id(HtmDict *d)            { return d ? d->id : ""; }
const char *htm_dict_zip_path(HtmDict *d)       { return d ? d->zip_path : ""; }
const char *htm_dict_display_name(HtmDict *d)   { return d && d->display_name ? d->display_name : "HTM Dictionary"; }
const char *htm_dict_index_lang(HtmDict *d)     { return d && d->index_lang   ? d->index_lang   : "Unknown"; }
const char *htm_dict_contents_lang(HtmDict *d)  { return d && d->contents_lang? d->contents_lang: "Unknown"; }
const char *htm_dict_resource_prefix(HtmDict *d){ return d && d->resource_prefix? d->resource_prefix : ""; }
ZipMmap    *htm_dict_peek_zip(HtmDict *d)        { return d ? d->zip : NULL; }
FlatIndex  *htm_dict_get_flat_index(HtmDict *d)  { return d ? d->flat_index : NULL; }

/* ────────────────────────────────────────────────────────────── */
/*  Helpers                                                        */
/* ────────────────────────────────────────────────────────────── */

static char *zip_basename_stem(const char *zip_path) {
    char *base = g_path_get_basename(zip_path);
    char *dot  = strrchr(base, '.');
    if (dot && g_ascii_strcasecmp(dot, ".diction") == 0)
        *dot = '\0';
    return base;
}

static char *discover_html_entry(ZipMmap *z, const char *zip_path) {
    char *stem = zip_basename_stem(zip_path);

    /* 1. stem.html at root */
    char *try1 = g_strdup_printf("%s.html", stem);
    if (zip_mmap_lookup(z, try1)) { g_free(stem); return try1; }
    g_free(try1);

    /* 2. stem/stem.html one level deep */
    char *try2 = g_strdup_printf("%s/%s.html", stem, stem);
    if (zip_mmap_lookup(z, try2)) { g_free(stem); return try2; }
    g_free(try2);

    /* 3. first .html at most one directory level */
    GPtrArray *names    = zip_mmap_list_names(z);
    char      *fallback = NULL;
    for (guint i = 0; names && i < names->len; i++) {
        const char *n     = g_ptr_array_index(names, i);
        if (!g_str_has_suffix(n, ".html")) continue;
        const char *slash = strchr(n, '/');
        if (slash && strchr(slash + 1, '/')) continue;
        if (!fallback) fallback = g_strdup(n);
    }
    g_free(stem);
    return fallback;
}

static char *resource_prefix_from_html_entry(const char *html_entry) {
    const char *slash = strrchr(html_entry, '/');
    if (!slash) return g_strdup("");
    return g_strndup(html_entry, (gsize)(slash - html_entry + 1));
}

/* ────────────────────────────────────────────────────────────── */
/*  Sidecar HTML (compressed entry → plain file on disk)           */
/* ────────────────────────────────────────────────────────────── */

static gboolean mmap_sidecar(HtmDict *d, GError **error) {
    struct stat st;
    if (stat(d->sidecar_path, &st) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "stat sidecar");
        return FALSE;
    }
    d->sidecar_fd = open(d->sidecar_path, O_RDONLY);
    if (d->sidecar_fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "open sidecar");
        return FALSE;
    }
    d->sidecar_size = (size_t)st.st_size;
    void *m = mmap(NULL, d->sidecar_size, PROT_READ, MAP_SHARED, d->sidecar_fd, 0);
    if (m == MAP_FAILED) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "mmap sidecar");
        close(d->sidecar_fd);
        d->sidecar_fd = -1;
        return FALSE;
    }
    d->sidecar_map  = (uint8_t *)m;
    d->html_base    = d->sidecar_map;
    d->html_size    = d->sidecar_size;
    d->html_is_sidecar = TRUE;
    return TRUE;
}

static gboolean ensure_sidecar_html(HtmDict *d, const ZipEntry *he, GError **error) {
    dict_cache_ensure_dir();
    gboolean need = !dict_cache_is_valid(d->sidecar_path, d->zip_path);
    if (g_file_test(d->sidecar_path, G_FILE_TEST_EXISTS)) {
        struct stat sst;
        if (stat(d->sidecar_path, &sst) == 0 && sst.st_size == 0)
            need = TRUE;
    }
    if (need) {
        size_t raw_len = 0;
        guint8 *raw = zip_mmap_read_entry(d->zip, he, &raw_len, error);
        if (!raw) return FALSE;
        if (!dict_cache_prepare_target_path(d->sidecar_path, (guint64)raw_len)) {
            g_free(raw);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOSPC, "cache space");
            return FALSE;
        }
        FILE *out = fopen(d->sidecar_path, "wb");
        if (!out) {
            g_free(raw);
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "write sidecar");
            return FALSE;
        }
        if (fwrite(raw, 1, raw_len, out) != raw_len) {
            fclose(out); g_free(raw); g_unlink(d->sidecar_path);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "short write sidecar");
            return FALSE;
        }
        fclose(out); g_free(raw);
        const char *srcs[] = {d->zip_path, NULL};
        dict_cache_sync_mtime(d->sidecar_path, srcs, 1);
    }
    return mmap_sidecar(d, error);
}

/* ────────────────────────────────────────────────────────────── */
/*  Meta-line parser (reads #NAME etc. from the HTML sidecar)     */
/* ────────────────────────────────────────────────────────────── */

typedef enum { S_META, S_HTML } ScanState;

static void parse_meta_line(const char *line, HtmDict *d) {
    if (line[0] != '#') return;
    const char *keys[]  = { "#NAME", "#INDEX_LANGUAGE", "#CONTENTS_LANGUAGE" };
    char      **fields[] = { &d->display_name, &d->index_lang, &d->contents_lang };
    for (int k = 0; k < 3; k++) {
        if (!g_str_has_prefix(line, keys[k])) continue;
        const char *q = strstr(line, "\""); if (!q) return; q++;
        const char *end = strrchr(q, '"');  if (!end || end <= q) return;
        g_free(*fields[k]);
        *fields[k] = g_strndup(q, (gsize)(end - q));
        return;
    }
}

/* ────────────────────────────────────────────────────────────── */
/*  Index builder                                                  */
/* ────────────────────────────────────────────────────────────── */

static char *strip_html_tags(const uint8_t *html, size_t len) {
    GString *out    = g_string_sized_new(len);
    gboolean in_tag = FALSE;
    for (size_t i = 0; i < len; i++) {
        if      (html[i] == '<') in_tag = TRUE;
        else if (html[i] == '>') in_tag = FALSE;
        else if (!in_tag)        g_string_append_c(out, (char)html[i]);
    }
    return g_string_free(out, FALSE);
}

static int cmp_tmp_entry(const void *a, const void *b) {
    const TmpEntry *x = *(const TmpEntry *const *)a;
    const TmpEntry *y = *(const TmpEntry *const *)b;
    return g_utf8_collate(x->word, y->word);
}

static void tmp_entry_free(gpointer p) {
    TmpEntry *e = p;
    g_free(e->word);
    g_free(e);
}

static void htm_resolve_links(GPtrArray *tmps, const uint8_t *buf, size_t size) {
    (void)size;
    if (tmps->len == 0) return;

    GHashTable *lookup = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < tmps->len; i++) {
        TmpEntry *te = g_ptr_array_index(tmps, i);
        if (!g_hash_table_contains(lookup, te->word)) {
            g_hash_table_insert(lookup, te->word, te);
        }
    }

    int resolved_count = 0;
    for (guint i = 0; i < tmps->len; i++) {
        TmpEntry *te = g_ptr_array_index(tmps, i);
        if (te->len >= 8 && strncmp((const char *)(buf + te->off), "@@@LINK=", 8) == 0) {
            char *link_content = g_strndup((const char *)(buf + te->off + 8), te->len - 8);
            char *target_word = g_strstrip(link_content);
            TmpEntry *target = g_hash_table_lookup(lookup, target_word);
            int depth = 0;
            while (target && depth < 20) {
                if (target->len >= 8 && strncmp((const char *)(buf + target->off), "@@@LINK=", 8) == 0) {
                    char *next_link = g_strndup((const char *)(buf + target->off + 8), target->len - 8);
                    char *next_target_word = g_strstrip(next_link);
                    TmpEntry *next_target = g_hash_table_lookup(lookup, next_target_word);
                    g_free(next_link);
                    if (!next_target || next_target == target) break;
                    target = next_target;
                    depth++;
                } else {
                    break;
                }
            }
            if (target && target != te) {
                te->off = target->off;
                te->len = target->len;
                resolved_count++;
            }
            g_free(link_content);
        }
    }
    if (resolved_count > 0) {
        g_message("Resolved %d dictionary links (@@@LINK=)", resolved_count);
    }
    g_hash_table_destroy(lookup);
}

static gboolean htm_build_index(HtmDict *d, const uint8_t *buf, size_t size, GError **error) {
    ScanState  state        = S_META;
    char      *current_word = NULL;
    guint64    html_start   = 0;
    size_t     pos          = 0;

    GPtrArray *tmps = g_ptr_array_new_with_free_func(tmp_entry_free);

    while (pos < size) {
        size_t line_start = pos;
        while (pos < size && buf[pos] != '\n' && buf[pos] != '\r')
            pos++;
        size_t line_end = pos;
        char  *line     = g_strndup((const char *)(buf + line_start), line_end - line_start);
        while (pos < size && (buf[pos] == '\n' || buf[pos] == '\r'))
            pos++;

        char *trim = g_strstrip(line);

        if (state == S_META) {
            if (trim[0] == '#') {
                parse_meta_line(trim, d);
            } else if (trim[0]) {
                g_free(current_word);
                current_word = g_strdup(trim);
                state      = S_HTML;
                html_start = (guint64)pos;
            }
        } else {
            if (strcmp(trim, "</>") == 0) {
                guint64 html_end = (guint64)line_start;
                if (current_word && html_end > html_start) {
                    if (html_end - html_start > G_MAXUINT32) {
                        g_free(current_word); g_free(line);
                        g_ptr_array_unref(tmps);
                        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "entry too large");
                        return FALSE;
                    }
                    TmpEntry *e = g_new(TmpEntry, 1);
                    e->word = g_strdup(current_word);
                    e->off  = html_start;
                    e->len  = (guint32)(html_end - html_start);
                    g_ptr_array_add(tmps, e);
                }
                state = S_META;
            }
        }
        g_free(line);
    }
    g_free(current_word);

    if (tmps->len == 0) {
        g_ptr_array_unref(tmps);
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "no entries found in HTML");
        return FALSE;
    }

    /* Resolve @@@LINK= entries */
    htm_resolve_links(tmps, buf, size);

    /* Sort entries alphabetically */
    g_ptr_array_sort(tmps, cmp_tmp_entry);

    /* Build flat cache + FTS index */
    DictCacheBuilder *cb = dict_cache_builder_new(d->flat_path, (uint64_t)tmps->len);
    if (!cb) {
        g_ptr_array_unref(tmps);
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "cannot create flat cache: %s", d->flat_path);
        return FALSE;
    }

    DictFtsBuilder *fb = dict_fts_builder_new(d->zip_path, NULL);

    FlatTreeEntry *entries = g_new0(FlatTreeEntry, tmps->len);

    for (guint i = 0; i < tmps->len; i++) {
        TmpEntry *te  = g_ptr_array_index(tmps, i);
        uint64_t h_off = 0;
        dict_cache_builder_add_headword(cb, te->word, strlen(te->word), &h_off);

        entries[i].h_off = (uint32_t)h_off;
        entries[i].h_len = (uint32_t)strlen(te->word);
        entries[i].d_off = (uint32_t)te->off;
        entries[i].d_len = te->len;

        if (fb) {
            char *def_text = strip_html_tags(buf + te->off, te->len);
            dict_fts_builder_add(fb, i,
                                 te->word, strlen(te->word),
                                 def_text, strlen(def_text));
            g_free(def_text);
        }
    }

    dict_cache_builder_finalize(cb, entries, (uint64_t)tmps->len);
    dict_cache_builder_free(cb);
    g_free(entries);
    g_ptr_array_unref(tmps);

    if (fb)
        dict_fts_builder_finish(fb, NULL);

    return TRUE;
}

/* ────────────────────────────────────────────────────────────── */
/*  Open / Close                                                   */
/* ────────────────────────────────────────────────────────────── */

void htm_dict_close(HtmDict *d) {
    if (!d) return;
    g_free(d->zip_path);
    g_free(d->id);
    g_free(d->display_name);
    g_free(d->index_lang);
    g_free(d->contents_lang);
    g_free(d->html_entry);
    g_free(d->resource_prefix);
    zip_mmap_close(d->zip);
    if (d->sidecar_map && d->sidecar_size)
        munmap(d->sidecar_map, d->sidecar_size);
    if (d->sidecar_fd >= 0)
        close(d->sidecar_fd);
    if (d->flat_index) flat_index_close(d->flat_index);
    if (d->flat_mf)    g_mapped_file_unref(d->flat_mf);
    g_free(d->sidecar_path);
    g_free(d->flat_path);
    g_free(d);
}

HtmDict *htm_dict_open(const char *zip_path, GError **error) {
    const char *dot = zip_path ? strrchr(zip_path, '.') : NULL;
    if (!dot || g_ascii_strcasecmp(dot, ".diction") != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "only .diction files are supported");
        return NULL;
    }

    dict_cache_ensure_dir();

    HtmDict *d      = g_new0(HtmDict, 1);
    d->zip_path     = g_strdup(zip_path);
    d->sidecar_fd   = -1;

    char *full_hash = g_compute_checksum_for_string(G_CHECKSUM_SHA1, zip_path, -1);
    d->id           = g_strndup(full_hash, 8);
    g_free(full_hash);

    d->zip = zip_mmap_open(zip_path, error);
    if (!d->zip) {
        g_free(d->id); g_free(d->zip_path); g_free(d);
        return NULL;
    }

    d->html_entry = discover_html_entry(d->zip, zip_path);
    if (!d->html_entry) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "no suitable .html inside zip");
        htm_dict_close(d);
        return NULL;
    }

    const ZipEntry *he = zip_mmap_lookup(d->zip, d->html_entry);
    if (!he) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "html entry missing");
        htm_dict_close(d);
        return NULL;
    }

    d->resource_prefix = resource_prefix_from_html_entry(d->html_entry);

    /* Cache paths */
    char *base      = dict_cache_path_for(zip_path);
    d->flat_path    = g_strconcat(base, ".flat",     NULL);
    d->sidecar_path = g_strconcat(base, ".html.bin", NULL);
    g_free(base);

    /* Decompress HTML if needed */
    if (he->method == 0) {
        d->html_base       = (const uint8_t *)zip_mmap_data(d->zip) + he->payload_offset;
        d->html_size       = (size_t)he->uncompressed_size;
        d->html_is_sidecar = FALSE;
    } else {
        if (!ensure_sidecar_html(d, he, error)) {
            htm_dict_close(d);
            return NULL;
        }
    }

    /* Default metadata */
    if (!d->display_name) d->display_name  = g_strdup(d->html_entry);
    if (!d->index_lang)   d->index_lang    = g_strdup("Unknown");
    if (!d->contents_lang)d->contents_lang = g_strdup("Unknown");

    /* Parse display_name etc. from the HTML buffer before building index */
    {
        const uint8_t *buf = d->html_base;
        size_t size        = d->html_size;
        size_t pos         = 0;
        /* Only scan the first 4 KB for meta lines */
        size_t scan_limit  = (size < 4096) ? size : 4096;
        while (pos < scan_limit) {
            size_t ls = pos;
            while (pos < scan_limit && buf[pos] != '\n' && buf[pos] != '\r') pos++;
            char *line = g_strndup((const char *)(buf + ls), pos - ls);
            while (pos < scan_limit && (buf[pos] == '\n' || buf[pos] == '\r')) pos++;
            char *trim = g_strstrip(line);
            if (trim[0] == '#') parse_meta_line(trim, d);
            else if (trim[0])   { g_free(line); break; } /* first headword: stop */
            g_free(line);
        }
    }

    /* Build flat index if not present */
    if (!g_file_test(d->flat_path, G_FILE_TEST_EXISTS)) {
        if (!htm_build_index(d, d->html_base, d->html_size, error)) {
            htm_dict_close(d);
            return NULL;
        }
    }

    /* mmap the flat cache */
    d->flat_mf = g_mapped_file_new(d->flat_path, FALSE, error);
    if (!d->flat_mf) {
        htm_dict_close(d);
        return NULL;
    }

    d->flat_index = flat_index_open(
        g_mapped_file_get_contents(d->flat_mf),
        g_mapped_file_get_length(d->flat_mf));

    (void)d->html_is_sidecar;
    return d;
}

/* ────────────────────────────────────────────────────────────── */
/*  Lookup by word (uses FlatIndex for O(log N) search)           */
/* ────────────────────────────────────────────────────────────── */

char *htm_dict_get_definition_html(HtmDict *d, const char *word) {
    if (!d || !word) return g_strdup("");

    /* Binary search in the flat index */
    if (d->flat_index) {
        size_t pos = flat_index_search(d->flat_index, word);
        if (pos == (size_t)-1) {
            /* try case-insensitive prefix */
            pos = flat_index_search_prefix(d->flat_index, word);
        }
        if (pos != (size_t)-1) {
            const FlatTreeEntry *e = flat_index_get(d->flat_index, pos);
            if (e && e->d_off + (guint64)e->d_len <= (guint64)d->html_size) {
                char *frag = g_strndup((const char *)(d->html_base + e->d_off), (gsize)e->d_len);
                return g_strstrip(frag);
            }
        }
    }
    return g_strdup("");
}

/* ────────────────────────────────────────────────────────────── */
/*  Resource bytes (images, audio, CSS …)                         */
/* ────────────────────────────────────────────────────────────── */

static const char *sniff_mime(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (g_ascii_strcasecmp(dot, ".jpg")  == 0 || g_ascii_strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (g_ascii_strcasecmp(dot, ".png")  == 0) return "image/png";
    if (g_ascii_strcasecmp(dot, ".gif")  == 0) return "image/gif";
    if (g_ascii_strcasecmp(dot, ".ogg")  == 0 || g_ascii_strcasecmp(dot, ".oga")  == 0) return "audio/ogg";
    if (g_ascii_strcasecmp(dot, ".opus") == 0) return "audio/opus";
    if (g_ascii_strcasecmp(dot, ".mp3")  == 0) return "audio/mpeg";
    if (g_ascii_strcasecmp(dot, ".spx")  == 0 || g_ascii_strcasecmp(dot, ".speex")== 0) return "audio/speex";
    if (g_ascii_strcasecmp(dot, ".css")  == 0) return "text/css";
    if (g_ascii_strcasecmp(dot, ".js")   == 0) return "text/javascript";
    if (g_ascii_strcasecmp(dot, ".html") == 0 || g_ascii_strcasecmp(dot, ".htm")  == 0) return "text/html";
    return "application/octet-stream";
}

gboolean htm_dict_read_resource_bytes(HtmDict *d, const char *archive_relative_path,
                                      GBytes **out_bytes, char **out_mime, GError **error) {
    if (!d || !archive_relative_path) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "bad args");
        return FALSE;
    }
    const ZipEntry *e = zip_mmap_lookup(d->zip, archive_relative_path);
    if (!e) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "not in zip: %s", archive_relative_path);
        return FALSE;
    }
    if (!zip_mmap_read_entry_bytes(d->zip, e, out_bytes, error))
        return FALSE;
    if (out_mime)
        *out_mime = g_strdup(sniff_mime(archive_relative_path));
    return TRUE;
}
