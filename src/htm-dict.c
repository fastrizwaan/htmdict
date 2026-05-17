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
#include <json-glib/json-glib.h>

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
    char *root_prefix;
    char *html_entry;
    char *resource_prefix;
    char *stylesheet;

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
const char *htm_dict_stylesheet(HtmDict *d)     { return d && d->stylesheet      ? d->stylesheet      : ""; }
ZipMmap    *htm_dict_peek_zip(HtmDict *d)        { return d ? d->zip : NULL; }
FlatIndex  *htm_dict_get_flat_index(HtmDict *d)  { return d ? d->flat_index : NULL; }

/* ────────────────────────────────────────────────────────────── */
/*  Helpers                                                        */
/* ────────────────────────────────────────────────────────────── */

static char *resource_prefix_from_html_entry(const char *html_entry) {
    const char *slash = strrchr(html_entry, '/');
    if (!slash) return g_strdup("");
    return g_strndup(html_entry, (gsize)(slash - html_entry + 1));
}

static gboolean archive_path_is_safe(const char *path) {
    if (!path || !*path) return FALSE;
    if (path[0] == '/' || path[0] == '\\') return FALSE;

    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (*p < 0x20 || *p == 0x7f || *p == '\\') return FALSE;
    }

    char **parts = g_strsplit(path, "/", -1);
    gboolean ok = TRUE;
    for (guint i = 0; parts[i]; i++) {
        if (parts[i][0] == '\0' && parts[i + 1] == NULL) {
            continue;
        }
        if (parts[i][0] == '\0' || g_strcmp0(parts[i], ".") == 0 || g_strcmp0(parts[i], "..") == 0) {
            ok = FALSE;
            break;
        }
    }
    g_strfreev(parts);
    return ok;
}

static gboolean resource_path_is_safe(const char *path) {
    if (!path || !*path) return FALSE;
    if (strstr(path, "://")) return FALSE;

    char *unescaped = g_uri_unescape_string(path, NULL);
    if (!unescaped) return FALSE;
    gboolean ok = archive_path_is_safe(unescaped);
    g_free(unescaped);
    return ok;
}

static gboolean discover_diction_root(ZipMmap *z, char **out_root, GError **error) {
    GPtrArray *names = zip_mmap_list_names(z);
    char *root = NULL;

    for (guint i = 0; names && i < names->len; i++) {
        const char *name = g_ptr_array_index(names, i);
        if (!name || !*name) continue;
        if (!archive_path_is_safe(name)) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "unsafe archive path: %s", name);
            g_free(root);
            return FALSE;
        }
        const char *slash = strchr(name, '/');
        if (!slash || slash == name) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, ".diction archive must contain exactly one top-level root directory");
            g_free(root);
            return FALSE;
        }
        char *candidate = g_strndup(name, (gsize)(slash - name));
        if (!root) {
            root = candidate;
        } else if (g_strcmp0(root, candidate) != 0) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, ".diction archive contains multiple top-level roots");
            g_free(candidate);
            g_free(root);
            return FALSE;
        } else {
            g_free(candidate);
        }
    }

    if (!root) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "empty .diction archive");
        return FALSE;
    }

    *out_root = g_strconcat(root, "/", NULL);
    g_free(root);
    return TRUE;
}

static gboolean meta_string_member(JsonObject *obj, const char *name, const char **out, GError **error) {
    if (!json_object_has_member(obj, name) || !JSON_NODE_HOLDS_VALUE(json_object_get_member(obj, name))) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "meta.json missing required string field: %s", name);
        return FALSE;
    }
    const char *s = json_object_get_string_member(obj, name);
    if (!s || !*s || !archive_path_is_safe(s)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "meta.json field %s is not a safe relative path/name", name);
        return FALSE;
    }
    *out = s;
    return TRUE;
}

static gboolean meta_required_string(JsonObject *obj, const char *name, GError **error) {
    if (!json_object_has_member(obj, name) || !JSON_NODE_HOLDS_VALUE(json_object_get_member(obj, name)) ||
        !json_object_get_string_member(obj, name)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "meta.json missing required string field: %s", name);
        return FALSE;
    }
    return TRUE;
}

static gboolean meta_required_string_array(JsonObject *obj, const char *name, GError **error) {
    if (!json_object_has_member(obj, name) || !JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, name))) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "meta.json missing required array field: %s", name);
        return FALSE;
    }
    JsonArray *arr = json_object_get_array_member(obj, name);
    if (json_array_get_length(arr) == 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "meta.json array field is empty: %s", name);
        return FALSE;
    }
    return TRUE;
}

static gboolean parse_meta_json(HtmDict *d, const char *json_path, GError **error) {
    const ZipEntry *ze = zip_mmap_lookup(d->zip, json_path);
    if (!ze) return FALSE;

    size_t len = 0;
    uint8_t *data = zip_mmap_read_entry(d->zip, ze, &len, error);
    if (!data) return FALSE;

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, (const char *)data, (gssize)len, error)) {
        g_free(data);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "meta.json root must be an object");
        g_object_unref(parser);
        g_free(data);
        return FALSE;
    }

    JsonObject *obj = json_node_get_object(root);
    if (!json_object_has_member(obj, "format") || json_object_get_int_member(obj, "format") != 1) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "meta.json format must be 1");
        g_object_unref(parser);
        g_free(data);
        return FALSE;
    }

    const char *html_val = NULL;
    const char *css_val = NULL;
    if (!meta_required_string(obj, "id", error) ||
        !meta_required_string(obj, "name", error) ||
        !meta_required_string(obj, "short_name", error) ||
        !meta_required_string_array(obj, "index_languages", error) ||
        !meta_required_string_array(obj, "content_languages", error) ||
        !meta_required_string(obj, "version", error) ||
        !meta_required_string(obj, "created", error) ||
        !meta_string_member(obj, "html", &html_val, error) ||
        !meta_string_member(obj, "stylesheet", &css_val, error)) {
        g_object_unref(parser);
        g_free(data);
        return FALSE;
    }

    if (json_object_has_member(obj, "name")) {
        g_free(d->display_name);
        d->display_name = g_strdup(json_object_get_string_member(obj, "name"));
    }
    g_free(d->stylesheet);
    d->stylesheet = g_strdup(css_val);

    g_free(d->html_entry);
    char *dir = g_path_get_dirname(json_path);
    if (g_strcmp0(dir, ".") == 0) d->html_entry = g_strdup(html_val);
    else                          d->html_entry = g_build_filename(dir, html_val, NULL);
    g_free(dir);

        if (json_object_has_member(obj, "index_languages")) {
            JsonArray *arr = json_object_get_array_member(obj, "index_languages");
            if (json_array_get_length(arr) > 0) {
                g_free(d->index_lang);
                d->index_lang = g_strdup(json_array_get_string_element(arr, 0));
            }
        }
        if (json_object_has_member(obj, "content_languages")) {
            JsonArray *arr = json_object_get_array_member(obj, "content_languages");
            if (json_array_get_length(arr) > 0) {
                g_free(d->contents_lang);
                d->contents_lang = g_strdup(json_array_get_string_element(arr, 0));
            }
        }

    g_object_unref(parser);
    g_free(data);
    return TRUE;
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

static const char *ascii_strcasestr_len(const char *haystack, size_t hay_len, const char *needle) {
    size_t needle_len = strlen(needle);
    if (!haystack || needle_len == 0 || hay_len < needle_len) return NULL;
    for (size_t i = 0; i <= hay_len - needle_len; i++) {
        if (g_ascii_strncasecmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

static gboolean tag_has_entry_class(const char *tag, size_t tag_len) {
    const char *p = tag;
    const char *end = tag + tag_len;
    while (p < end) {
        const char *class_attr = ascii_strcasestr_len(p, (size_t)(end - p), "class");
        if (!class_attr) return FALSE;
        p = class_attr + 5;
        while (p < end && g_ascii_isspace(*p)) p++;
        if (p >= end || *p != '=') continue;
        p++;
        while (p < end && g_ascii_isspace(*p)) p++;
        if (p >= end || (*p != '"' && *p != '\'')) continue;
        char quote = *p++;
        const char *value = p;
        while (p < end && *p != quote) p++;
        size_t value_len = (size_t)(p - value);
        size_t i = 0;
        while (i < value_len) {
            while (i < value_len && g_ascii_isspace(value[i])) i++;
            size_t start = i;
            while (i < value_len && !g_ascii_isspace(value[i])) i++;
            if (i > start && i - start == 5 && g_ascii_strncasecmp(value + start, "entry", 5) == 0)
                return TRUE;
        }
    }
    return FALSE;
}

static char *html_unescape_text(const char *s) {
    GString *out = g_string_new(NULL);
    for (const char *p = s; p && *p; ) {
        if (*p != '&') {
            g_string_append_c(out, *p++);
            continue;
        }
        const char *semi = strchr(p, ';');
        if (!semi || semi - p > 16) {
            g_string_append_c(out, *p++);
            continue;
        }
        char *entity = g_strndup(p + 1, (gsize)(semi - p - 1));
        gunichar ch = 0;
        if (g_strcmp0(entity, "amp") == 0) ch = '&';
        else if (g_strcmp0(entity, "lt") == 0) ch = '<';
        else if (g_strcmp0(entity, "gt") == 0) ch = '>';
        else if (g_strcmp0(entity, "quot") == 0) ch = '"';
        else if (g_strcmp0(entity, "apos") == 0 || g_strcmp0(entity, "#39") == 0) ch = '\'';
        else if (entity[0] == '#') {
            if (entity[1] == 'x' || entity[1] == 'X')
                ch = (gunichar)g_ascii_strtoull(entity + 2, NULL, 16);
            else
                ch = (gunichar)g_ascii_strtoull(entity + 1, NULL, 10);
        }
        if (ch) {
            char buf[8] = {0};
            gint n = g_unichar_to_utf8(ch, buf);
            g_string_append_len(out, buf, n);
            p = semi + 1;
        } else {
            g_string_append_len(out, p, (gssize)(semi + 1 - p));
            p = semi + 1;
        }
        g_free(entity);
    }
    return g_string_free(out, FALSE);
}

static const char *find_entry_article_start(const char *buf, size_t size, size_t pos, const char **out_tag_end) {
    while (pos < size) {
        const char *p = buf + pos;
        const char *start = ascii_strcasestr_len(p, size - pos, "<article");
        if (!start) return NULL;
        const char *tag_end = memchr(start, '>', size - (size_t)(start - buf));
        if (!tag_end) return NULL;
        if (tag_has_entry_class(start, (size_t)(tag_end + 1 - start))) {
            if (out_tag_end) *out_tag_end = tag_end;
            return start;
        }
        pos = (size_t)(tag_end + 1 - buf);
    }
    return NULL;
}

static char *extract_attr_value(const char *tag, size_t tag_len, const char *attr_name) {
    const char *p = tag;
    const char *end = tag + tag_len;
    size_t attr_len = strlen(attr_name);
    while (p < end) {
        const char *found = ascii_strcasestr_len(p, (size_t)(end - p), attr_name);
        if (!found) return NULL;
        gboolean left_ok = found == tag || g_ascii_isspace(found[-1]) || found[-1] == '<';
        const char *q = found + attr_len;
        gboolean right_ok = q < end && (g_ascii_isspace(*q) || *q == '=');
        if (!left_ok || !right_ok) {
            p = found + attr_len;
            continue;
        }
        while (q < end && g_ascii_isspace(*q)) q++;
        if (q >= end || *q != '=') return NULL;
        q++;
        while (q < end && g_ascii_isspace(*q)) q++;
        if (q >= end || (*q != '"' && *q != '\'')) return NULL;
        char quote = *q++;
        const char *value = q;
        while (q < end && *q != quote) q++;
        if (q >= end) return NULL;
        return g_strndup(value, (gsize)(q - value));
    }
    return NULL;
}

static char *extract_element_visible_text(const char *html, size_t len, const char *element, size_t *io_pos) {
    size_t pos = io_pos ? *io_pos : 0;
    char *open_pat = g_strdup_printf("<%s", element);
    char *close_pat = g_strdup_printf("</%s>", element);
    const char *open = ascii_strcasestr_len(html + pos, len - pos, open_pat);
    g_free(open_pat);
    if (!open) {
        g_free(close_pat);
        return NULL;
    }
    const char *open_end = memchr(open, '>', len - (size_t)(open - html));
    if (!open_end) {
        g_free(close_pat);
        return NULL;
    }
    const char *content = open_end + 1;
    const char *close = ascii_strcasestr_len(content, len - (size_t)(content - html), close_pat);
    g_free(close_pat);
    if (!close) return NULL;

    char *stripped = strip_html_tags((const uint8_t *)content, (size_t)(close - content));
    char *unescaped = html_unescape_text(stripped);
    g_free(stripped);
    if (io_pos) *io_pos = (size_t)(close - html) + strlen(element) + 3;
    return unescaped ? unescaped : g_strdup("");
}

static gboolean is_zero_width_format(gunichar ch) {
    return ch == 0x200B || ch == 0x200C || ch == 0x200D || ch == 0xFEFF;
}

static char *diction_normalize_key(const char *text) {
    if (!text) return NULL;
    GString *tmp = g_string_new(NULL);
    gboolean in_ws = FALSE;

    for (const char *p = text; p && *p; ) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
            p++;
            continue;
        }
        p = g_utf8_next_char(p);
        if (is_zero_width_format(ch)) continue;
        if (g_unichar_isspace(ch)) {
            if (!in_ws && tmp->len > 0) {
                g_string_append_c(tmp, ' ');
                in_ws = TRUE;
            }
            continue;
        }
        in_ws = FALSE;
        char buf[8] = {0};
        gint n = g_unichar_to_utf8(ch, buf);
        g_string_append_len(tmp, buf, n);
    }

    if (tmp->len > 0 && tmp->str[tmp->len - 1] == ' ')
        g_string_truncate(tmp, tmp->len - 1);

    char *nfc = g_utf8_normalize(tmp->str, -1, G_NORMALIZE_NFC);
    g_string_free(tmp, TRUE);
    if (!nfc) return g_strdup("");

    GString *folded = g_string_new(NULL);
    for (const char *p = nfc; *p; ) {
        gunichar ch = g_utf8_get_char(p);
        p = g_utf8_next_char(p);
        ch = g_unichar_tolower(ch);
        char buf[8] = {0};
        gint n = g_unichar_to_utf8(ch, buf);
        g_string_append_len(folded, buf, n);
    }
    g_free(nfc);
    return g_string_free(folded, FALSE);
}

static void add_index_key(GPtrArray *tmps, GHashTable *seen, const char *raw_key, guint64 off, guint32 len) {
    char *key = diction_normalize_key(raw_key);
    if (!key || !*key) {
        g_free(key);
        return;
    }

    char *dedupe = g_strdup_printf("%s:%" G_GUINT64_FORMAT ":%u", key, off, len);
    if (!g_hash_table_contains(seen, dedupe)) {
        TmpEntry *e = g_new(TmpEntry, 1);
        e->word = key;
        e->off = off;
        e->len = len;
        g_ptr_array_add(tmps, e);
        g_hash_table_add(seen, dedupe);
    } else {
        g_free(key);
        g_free(dedupe);
    }
}

static gboolean tag_name_matches(const char *tag, size_t tag_len, const char *name) {
    size_t i = 1;
    if (i < tag_len && tag[i] == '/') i++;
    while (i < tag_len && g_ascii_isspace(tag[i])) i++;
    size_t nlen = strlen(name);
    return i + nlen <= tag_len &&
           g_ascii_strncasecmp(tag + i, name, nlen) == 0 &&
           (i + nlen == tag_len || g_ascii_isspace(tag[i + nlen]) || tag[i + nlen] == '>' || tag[i + nlen] == '/');
}

static gboolean attr_name_is_event(const char *name, size_t len) {
    return len >= 2 && (name[0] == 'o' || name[0] == 'O') && (name[1] == 'n' || name[1] == 'N');
}

static gboolean attr_value_is_javascript(const char *value, size_t len) {
    while (len > 0 && g_ascii_isspace(*value)) {
        value++;
        len--;
    }
    return len >= 11 && g_ascii_strncasecmp(value, "javascript:", 11) == 0;
}

static void append_sanitized_tag(GString *out, const char *tag, size_t tag_len) {
    if (tag_len < 2 || tag[0] != '<') return;
    if (tag_name_matches(tag, tag_len, "form")) {
        if (tag_len > 1 && tag[1] == '/') g_string_append(out, "</div>");
        else g_string_append(out, "<div");
    } else {
        size_t name_end = 1;
        if (name_end < tag_len && tag[name_end] == '/') name_end++;
        while (name_end < tag_len && !g_ascii_isspace(tag[name_end]) && tag[name_end] != '>' && tag[name_end] != '/')
            name_end++;
        g_string_append_len(out, tag, name_end);
    }

    size_t i = 1;
    if (i < tag_len && tag[i] == '/') i++;
    while (i < tag_len && !g_ascii_isspace(tag[i]) && tag[i] != '>' && tag[i] != '/') i++;

    while (i < tag_len && tag[i] != '>') {
        while (i < tag_len && g_ascii_isspace(tag[i])) i++;
        if (i >= tag_len || tag[i] == '>' || tag[i] == '/') break;
        size_t name_start = i;
        while (i < tag_len && !g_ascii_isspace(tag[i]) && tag[i] != '=' && tag[i] != '>' && tag[i] != '/') i++;
        size_t name_len = i - name_start;
        while (i < tag_len && g_ascii_isspace(tag[i])) i++;
        const char *value = NULL;
        size_t value_len = 0;
        size_t attr_end = i;
        if (i < tag_len && tag[i] == '=') {
            i++;
            while (i < tag_len && g_ascii_isspace(tag[i])) i++;
            if (i < tag_len && (tag[i] == '"' || tag[i] == '\'')) {
                char quote = tag[i++];
                value = tag + i;
                while (i < tag_len && tag[i] != quote) i++;
                value_len = (size_t)(tag + i - value);
                if (i < tag_len) i++;
                attr_end = i;
            } else {
                value = tag + i;
                while (i < tag_len && !g_ascii_isspace(tag[i]) && tag[i] != '>') i++;
                value_len = (size_t)(tag + i - value);
                attr_end = i;
            }
        }

        gboolean drop = attr_name_is_event(tag + name_start, name_len);
        if (!drop && value &&
            (g_ascii_strncasecmp(tag + name_start, "href", name_len) == 0 ||
             g_ascii_strncasecmp(tag + name_start, "src", name_len) == 0 ||
             g_ascii_strncasecmp(tag + name_start, "xlink:href", name_len) == 0)) {
            drop = attr_value_is_javascript(value, value_len);
        }
        if (!drop && name_len > 0) {
            g_string_append_c(out, ' ');
            g_string_append_len(out, tag + name_start, attr_end - name_start);
        }
    }

    if (tag_len >= 2 && tag[tag_len - 2] == '/') g_string_append(out, " /");
    g_string_append_c(out, '>');
}

static char *sanitize_diction_fragment(const char *html, size_t len) {
    GString *out = g_string_sized_new(len);
    size_t pos = 0;
    while (pos < len) {
        const char *lt = memchr(html + pos, '<', len - pos);
        if (!lt) {
            g_string_append_len(out, html + pos, len - pos);
            break;
        }
        g_string_append_len(out, html + pos, (size_t)(lt - (html + pos)));
        const char *gt = memchr(lt, '>', len - (size_t)(lt - html));
        if (!gt) break;
        size_t tag_len = (size_t)(gt + 1 - lt);

        if (tag_name_matches(lt, tag_len, "script") || tag_name_matches(lt, tag_len, "iframe")) {
            const char *close = tag_name_matches(lt, tag_len, "script")
                ? ascii_strcasestr_len(gt + 1, len - (size_t)(gt + 1 - html), "</script>")
                : ascii_strcasestr_len(gt + 1, len - (size_t)(gt + 1 - html), "</iframe>");
            pos = close ? (size_t)(close - html) + (tag_name_matches(lt, tag_len, "script") ? 9 : 9)
                        : (size_t)(gt + 1 - html);
            continue;
        }

        append_sanitized_tag(out, lt, tag_len);
        pos = (size_t)(gt + 1 - html);
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
        
        const char *content_ptr = (const char *)(buf + te->off);
        size_t      content_len = te->len;

        /* Skip any leading tags (like <article> or <a>) to find @@@LINK= */
        while (content_len > 0 && *content_ptr == '<') {
            const char *tag_end = memchr(content_ptr, '>', content_len);
            if (tag_end) {
                size_t tag_len = (size_t)(tag_end + 1 - content_ptr);
                content_ptr += tag_len;
                content_len -= tag_len;
                /* Also skip any immediate whitespace after tag */
                while (content_len > 0 && g_ascii_isspace(*content_ptr)) {
                    content_ptr++;
                    content_len--;
                }
            } else {
                break;
            }
        }

        if (content_len >= 8 && strncmp(content_ptr, "@@@LINK=", 8) == 0) {
            const char *target_start = content_ptr + 8;
            const char *target_end = target_start;
            while (target_end < content_ptr + content_len && 
                   *target_end != '<' && 
                   !g_ascii_isspace(*target_end)) {
                target_end++;
            }
            
            char *target_word = g_strndup(target_start, (gsize)(target_end - target_start));
            TmpEntry *target = g_hash_table_lookup(lookup, target_word);
            int depth = 0;
            while (target && depth < 20) {
                const char *t_ptr = (const char *)(buf + target->off);
                size_t      t_len = target->len;

                /* Skip leading tags in target content as well */
                while (t_len > 0 && *t_ptr == '<') {
                    const char *t_tag_end = memchr(t_ptr, '>', t_len);
                    if (t_tag_end) {
                        size_t t_tag_len = (size_t)(t_tag_end + 1 - t_ptr);
                        t_ptr += t_tag_len;
                        t_len -= t_tag_len;
                        while (t_len > 0 && g_ascii_isspace(*t_ptr)) {
                            t_ptr++;
                            t_len--;
                        }
                    } else {
                        break;
                    }
                }

                if (t_len >= 8 && strncmp(t_ptr, "@@@LINK=", 8) == 0) {
                    const char *nt_start = t_ptr + 8;
                    const char *nt_end = nt_start;
                    while (nt_end < t_ptr + t_len && 
                           *nt_end != '<' && 
                           !g_ascii_isspace(*nt_end)) {
                        nt_end++;
                    }
                    char *next_target_word = g_strndup(nt_start, (gsize)(nt_end - nt_start));
                    TmpEntry *next_target = g_hash_table_lookup(lookup, next_target_word);
                    g_free(next_target_word);
                    
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
            g_free(target_word);
        }
    }
    if (resolved_count > 0) {
        g_message("Resolved %d dictionary links (@@@LINK=)", resolved_count);
    }
    g_hash_table_destroy(lookup);
}

static gboolean htm_build_index(HtmDict *d, const uint8_t *buf, size_t size, GError **error) {
    size_t     pos          = 0;

    GPtrArray *tmps = g_ptr_array_new_with_free_func(tmp_entry_free);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const char *html = (const char *)buf;

    while (pos < size) {
        const char *tag_end = NULL;
        const char *start = find_entry_article_start(html, size, pos, &tag_end);
        if (!start) break;

        const char *end = ascii_strcasestr_len(tag_end + 1, size - (size_t)(tag_end + 1 - html), "</article>");
        if (!end) break;

        guint64 html_start = (guint64)(start - html);
        guint64 html_end = (guint64)(end + 10 - html);
        guint32 html_len = (guint32)(html_end - html_start);
        const char *article = start;
        size_t article_len = (size_t)(html_end - html_start);

        gboolean has_lookup_key = FALSE;
        size_t scan = 0;
        char *headword = extract_element_visible_text(article, article_len, "headword", &scan);
        if (headword && *headword) {
            add_index_key(tmps, seen, headword, html_start, html_len);
            has_lookup_key = TRUE;
        }
        g_free(headword);

        scan = 0;
        for (;;) {
            char *alias = extract_element_visible_text(article, article_len, "alias", &scan);
            if (!alias) break;
            if (*alias) {
                add_index_key(tmps, seen, alias, html_start, html_len);
                has_lookup_key = TRUE;
            }
            g_free(alias);
        }

        scan = 0;
        for (;;) {
            char *infl = extract_element_visible_text(article, article_len, "inflection", &scan);
            if (!infl) break;
            if (*infl) {
                add_index_key(tmps, seen, infl, html_start, html_len);
                has_lookup_key = TRUE;
            }
            g_free(infl);
        }

        /* Compatibility for older generated packages that used article id as the key. */
        if (!has_lookup_key) {
            char *id = extract_attr_value(start, (size_t)(tag_end + 1 - start), "id");
            if (id && *id) add_index_key(tmps, seen, id, html_start, html_len);
            g_free(id);
        }

        pos = (size_t)(end + 10 - html);
    }
    g_hash_table_destroy(seen);

    if (tmps->len == 0) {
        g_ptr_array_unref(tmps);
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "no entries found in HTML (size=%zu)", size);
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
    g_free(d->root_prefix);
    g_free(d->html_entry);
    g_free(d->resource_prefix);
    g_free(d->stylesheet);
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

    if (!discover_diction_root(d->zip, &d->root_prefix, error)) {
        htm_dict_close(d);
        return NULL;
    }

    char *meta_path = g_strconcat(d->root_prefix, "meta.json", NULL);
    if (!zip_mmap_lookup(d->zip, meta_path)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "required meta.json missing: %s", meta_path);
        g_free(meta_path);
        htm_dict_close(d);
        return NULL;
    }

    if (!parse_meta_json(d, meta_path, error)) {
        g_free(meta_path);
        htm_dict_close(d);
        return NULL;
    }
    g_free(meta_path);

    if (!d->html_entry) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "no suitable .html inside zip");
        htm_dict_close(d);
        return NULL;
    }

    const ZipEntry *he = zip_mmap_lookup(d->zip, d->html_entry);
    if (!he) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "html entry missing: %s", d->html_entry);
        htm_dict_close(d);
        return NULL;
    }

    d->resource_prefix = resource_prefix_from_html_entry(d->html_entry);

    char *stylesheet_entry = g_strconcat(d->resource_prefix ? d->resource_prefix : "", d->stylesheet ? d->stylesheet : "", NULL);
    if (!d->stylesheet || !*d->stylesheet || !zip_mmap_lookup(d->zip, stylesheet_entry)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "stylesheet entry missing: %s", stylesheet_entry);
        g_free(stylesheet_entry);
        htm_dict_close(d);
        return NULL;
    }
    g_free(stylesheet_entry);

    /* Cache paths */
    char *base      = dict_cache_path_for(zip_path);
    d->flat_path    = g_strconcat(base, ".diction-v2.flat", NULL);
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

    /* Default metadata if still missing */
    if (!d->display_name) d->display_name  = g_strdup(d->html_entry);
    if (!d->index_lang)   d->index_lang    = g_strdup("Unknown");
    if (!d->contents_lang)d->contents_lang = g_strdup("Unknown");


    /* Build flat index if not present or stale. */
    if (!dict_cache_is_valid(d->flat_path, d->zip_path)) {
        if (!htm_build_index(d, d->html_base, d->html_size, error)) {
            htm_dict_close(d);
            return NULL;
        }
        const char *srcs[] = {d->zip_path, NULL};
        dict_cache_sync_mtime(d->flat_path, srcs, 1);
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
    char *query = diction_normalize_key(word);
    if (!query || !*query) {
        g_free(query);
        return g_strdup("");
    }

    /* Binary search in the flat index */
    if (d->flat_index) {
        size_t pos = flat_index_search(d->flat_index, query);
        if (pos == (size_t)-1) {
            /* try case-insensitive prefix */
            pos = flat_index_search_prefix(d->flat_index, query);
        }
        if (pos != (size_t)-1) {
            const FlatTreeEntry *e = flat_index_get(d->flat_index, pos);
            if (e && e->d_off + (guint64)e->d_len <= (guint64)d->html_size) {
                char *frag = sanitize_diction_fragment((const char *)(d->html_base + e->d_off), (size_t)e->d_len);
                g_free(query);
                return g_strstrip(frag);
            }
        }
    }
    g_free(query);
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
    if (!resource_path_is_safe(archive_relative_path)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "unsafe resource path: %s", archive_relative_path);
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
