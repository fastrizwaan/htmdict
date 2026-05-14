#include "diction-convert.h"
#include "zip-mmap.h"
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    char *name;
    char *index_languages;
    char *content_languages;
} DslMeta;

typedef enum {
    DSL_ENCODING_UNKNOWN,
    DSL_ENCODING_UTF8,
    DSL_ENCODING_UTF16LE,
    DSL_ENCODING_UTF16BE
} DslEncoding;

typedef struct {
    FILE *file;
    gzFile gz;
    GByteArray *buffer;
    DslEncoding encoding;
    gboolean eof;
    gboolean first_line;
} DslLineReader;

static gboolean path_has_suffix_ci(const char *path, const char *suffix) {
    if (!path || !suffix) return FALSE;
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > path_len) return FALSE;
    return g_ascii_strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

static void byte_array_drop_prefix(GByteArray *array, guint len) {
    if (len > 0) g_byte_array_remove_range(array, 0, len);
}

static gboolean dsl_line_reader_fill(DslLineReader *reader, GError **error) {
    if (reader->eof) return TRUE;

    guint8 tmp[8192];
    int nread = 0;
    if (reader->gz) {
        nread = gzread(reader->gz, tmp, sizeof(tmp));
        if (nread < 0) {
            int zerr = 0;
            const char *msg = gzerror(reader->gz, &zerr);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Could not read compressed DSL: %s", msg ? msg : "zlib error");
            return FALSE;
        }
    } else {
        nread = (int)fread(tmp, 1, sizeof(tmp), reader->file);
        if (nread == 0 && ferror(reader->file)) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not read DSL");
            return FALSE;
        }
    }

    if (nread == 0) {
        reader->eof = TRUE;
    } else {
        g_byte_array_append(reader->buffer, tmp, (guint)nread);
    }
    return TRUE;
}

static gboolean dsl_line_reader_detect_encoding(DslLineReader *reader, GError **error) {
    while (reader->buffer->len < 3 && !reader->eof) {
        if (!dsl_line_reader_fill(reader, error)) return FALSE;
    }

    if (reader->buffer->len >= 2 && reader->buffer->data[0] == 0xff && reader->buffer->data[1] == 0xfe) {
        reader->encoding = DSL_ENCODING_UTF16LE;
        byte_array_drop_prefix(reader->buffer, 2);
    } else if (reader->buffer->len >= 2 && reader->buffer->data[0] == 0xfe && reader->buffer->data[1] == 0xff) {
        reader->encoding = DSL_ENCODING_UTF16BE;
        byte_array_drop_prefix(reader->buffer, 2);
    } else if (reader->buffer->len >= 3 &&
               reader->buffer->data[0] == 0xef &&
               reader->buffer->data[1] == 0xbb &&
               reader->buffer->data[2] == 0xbf) {
        reader->encoding = DSL_ENCODING_UTF8;
        byte_array_drop_prefix(reader->buffer, 3);
    } else {
        reader->encoding = DSL_ENCODING_UTF8;
    }
    return TRUE;
}

static gboolean dsl_line_reader_open(DslLineReader *reader, const char *path, GError **error) {
    memset(reader, 0, sizeof(*reader));
    reader->first_line = TRUE;
    reader->encoding = DSL_ENCODING_UNKNOWN;
    reader->buffer = g_byte_array_new();

    if (path_has_suffix_ci(path, ".dz")) {
        reader->gz = gzopen(path, "rb");
        if (!reader->gz) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not open compressed DSL %s", path);
            g_byte_array_unref(reader->buffer);
            reader->buffer = NULL;
            return FALSE;
        }
    } else {
        reader->file = fopen(path, "rb");
        if (!reader->file) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not open %s", path);
            g_byte_array_unref(reader->buffer);
            reader->buffer = NULL;
            return FALSE;
        }
    }

    return dsl_line_reader_detect_encoding(reader, error);
}

static void dsl_line_reader_close(DslLineReader *reader) {
    if (reader->gz) {
        gzclose(reader->gz);
        reader->gz = NULL;
    }
    if (reader->file) {
        fclose(reader->file);
        reader->file = NULL;
    }
    if (reader->buffer) {
        g_byte_array_unref(reader->buffer);
        reader->buffer = NULL;
    }
}

static gboolean dsl_line_reader_convert_line(DslLineReader *reader, guint line_len, guint remove_len, char **line, GError **error) {
    if (reader->encoding == DSL_ENCODING_UTF8) {
        guint len = line_len;
        if (len > 0 && reader->buffer->data[len - 1] == '\r') len--;
        *line = g_strndup((const char *)reader->buffer->data, len);
    } else {
        guint len = line_len;
        if (len >= 2) {
            gboolean trailing_cr = reader->encoding == DSL_ENCODING_UTF16LE
                ? (reader->buffer->data[len - 2] == '\r' && reader->buffer->data[len - 1] == '\0')
                : (reader->buffer->data[len - 2] == '\0' && reader->buffer->data[len - 1] == '\r');
            if (trailing_cr) len -= 2;
        }

        const char *from_codeset = reader->encoding == DSL_ENCODING_UTF16LE ? "UTF-16LE" : "UTF-16BE";
        gsize bytes_read = 0;
        gsize bytes_written = 0;
        *line = g_convert((const char *)reader->buffer->data, len, "UTF-8", from_codeset,
                          &bytes_read, &bytes_written, error);
        if (!*line) return FALSE;
    }

    byte_array_drop_prefix(reader->buffer, remove_len);
    return TRUE;
}

static gboolean dsl_line_reader_read_line(DslLineReader *reader, char **line, GError **error) {
    g_free(*line);
    *line = NULL;

    for (;;) {
        if (reader->encoding == DSL_ENCODING_UTF8) {
            for (guint i = 0; i < reader->buffer->len; i++) {
                if (reader->buffer->data[i] == '\n') {
                    gboolean ok = dsl_line_reader_convert_line(reader, i, i + 1, line, error);
                    reader->first_line = FALSE;
                    return ok;
                }
            }
        } else {
            for (guint i = 0; i + 1 < reader->buffer->len; i += 2) {
                gboolean is_newline = reader->encoding == DSL_ENCODING_UTF16LE
                    ? (reader->buffer->data[i] == '\n' && reader->buffer->data[i + 1] == '\0')
                    : (reader->buffer->data[i] == '\0' && reader->buffer->data[i + 1] == '\n');
                if (is_newline) {
                    gboolean ok = dsl_line_reader_convert_line(reader, i, i + 2, line, error);
                    reader->first_line = FALSE;
                    return ok;
                }
            }
        }

        if (reader->eof) {
            if (reader->buffer->len == 0) return FALSE;
            gboolean ok = dsl_line_reader_convert_line(reader, reader->buffer->len, reader->buffer->len, line, error);
            reader->first_line = FALSE;
            return ok;
        }

        if (!dsl_line_reader_fill(reader, error)) return FALSE;
    }
}

static char *stem_from_path(const char *path) {
    char *base = g_path_get_basename(path);
    char *stem = g_strdup(base);
    if (path_has_suffix_ci(stem, ".dsl.dz")) {
        stem[strlen(stem) - strlen(".dsl.dz")] = '\0';
    } else if (path_has_suffix_ci(stem, ".dsl")) {
        stem[strlen(stem) - strlen(".dsl")] = '\0';
    } else {
        char *ext = strrchr(stem, '.');
        if (ext) *ext = '\0';
    }
    g_free(base);
    return stem;
}

static gboolean rel_path_is_safe(const char *path) {
    if (!path || !path[0]) return FALSE;
    if (g_path_is_absolute(path)) return FALSE;
    char **parts = g_strsplit(path, "/", -1);
    gboolean safe = TRUE;
    for (guint i = 0; parts[i]; i++) {
        if (g_strcmp0(parts[i], "..") == 0) {
            safe = FALSE;
            break;
        }
    }
    g_strfreev(parts);
    return safe;
}

static gboolean write_bytes_to_media(const DictionOutput *out, const char *rel_path, const guint8 *data, gsize len, GError **error) {
    if (!rel_path_is_safe(rel_path)) return TRUE;

    char *media_path = g_build_filename(out->dict_dir, "media", rel_path, NULL);
    char *dir = g_path_get_dirname(media_path);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create media directory %s", dir);
        g_free(dir);
        g_free(media_path);
        return FALSE;
    }
    g_free(dir);

    gboolean ok = g_file_set_contents(media_path, (const char *)data, (gssize)len, error);
    g_free(media_path);
    return ok;
}

static gboolean copy_media_directory_recursive(const DictionOutput *out, const char *src_dir, const char *rel_prefix, GError **error) {
    GDir *dir = g_dir_open(src_dir, 0, error);
    if (!dir) return FALSE;

    gboolean ok = TRUE;
    const char *name = NULL;
    while (ok && (name = g_dir_read_name(dir))) {
        if (g_strcmp0(name, ".") == 0 || g_strcmp0(name, "..") == 0) continue;

        char *src_path = g_build_filename(src_dir, name, NULL);
        char *rel_path = rel_prefix && rel_prefix[0] ? g_build_filename(rel_prefix, name, NULL) : g_strdup(name);

        if (g_file_test(src_path, G_FILE_TEST_IS_DIR)) {
            ok = copy_media_directory_recursive(out, src_path, rel_path, error);
        } else if (g_file_test(src_path, G_FILE_TEST_IS_REGULAR)) {
            gchar *contents = NULL;
            gsize len = 0;
            if (g_file_get_contents(src_path, &contents, &len, error)) {
                ok = write_bytes_to_media(out, rel_path, (const guint8 *)contents, len, error);
                g_free(contents);
            } else {
                ok = FALSE;
            }
        }

        g_free(src_path);
        g_free(rel_path);
    }

    g_dir_close(dir);
    return ok;
}

static gboolean extract_media_zip(const DictionOutput *out, const char *zip_path, GError **error) {
    ZipMmap *zip = zip_mmap_open(zip_path, error);
    if (!zip) return FALSE;

    gboolean ok = TRUE;
    GPtrArray *names = zip_mmap_list_names(zip);
    for (guint i = 0; ok && names && i < names->len; i++) {
        const char *name = g_ptr_array_index(names, i);
        if (!name || !name[0] || g_str_has_suffix(name, "/")) continue;

        const ZipEntry *entry = zip_mmap_lookup(zip, name);
        if (!entry) continue;

        size_t len = 0;
        guint8 *data = zip_mmap_read_entry(zip, entry, &len, error);
        if (!data) {
            ok = FALSE;
            break;
        }
        ok = write_bytes_to_media(out, name, data, len, error);
        g_free(data);
    }

    zip_mmap_close(zip);
    return ok;
}

static void add_existing_path(GPtrArray *paths, const char *path) {
    if (path && g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_ptr_array_add(paths, g_strdup(path));
    }
}

static GPtrArray *find_related_media_paths(const char *input_path) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    char *direct_zip = g_strdup_printf("%s.files.zip", input_path);
    char *direct_dir = g_strdup_printf("%s.files", input_path);
    add_existing_path(paths, direct_zip);
    add_existing_path(paths, direct_dir);
    g_free(direct_zip);
    g_free(direct_dir);

    if (path_has_suffix_ci(input_path, ".dsl.dz")) {
        char *dsl_path = g_strndup(input_path, strlen(input_path) - strlen(".dz"));
        char *zip_path = g_strdup_printf("%s.files.zip", dsl_path);
        char *dir_path = g_strdup_printf("%s.files", dsl_path);
        add_existing_path(paths, zip_path);
        add_existing_path(paths, dir_path);
        g_free(zip_path);
        g_free(dir_path);
        g_free(dsl_path);
    } else if (path_has_suffix_ci(input_path, ".dsl")) {
        char *dz_path = g_strdup_printf("%s.dz", input_path);
        char *zip_path = g_strdup_printf("%s.files.zip", dz_path);
        char *dir_path = g_strdup_printf("%s.files", dz_path);
        add_existing_path(paths, zip_path);
        add_existing_path(paths, dir_path);
        g_free(zip_path);
        g_free(dir_path);
        g_free(dz_path);
    }

    return paths;
}

static gboolean extract_related_media(const char *input_path, const DictionOutput *out, GError **error) {
    GPtrArray *paths = find_related_media_paths(input_path);
    gboolean ok = TRUE;

    for (guint i = 0; ok && i < paths->len; i++) {
        const char *path = g_ptr_array_index(paths, i);
        fprintf(stderr, "Bundling DSL media: %s\n", path);
        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            ok = copy_media_directory_recursive(out, path, "", error);
        } else {
            ok = extract_media_zip(out, path, error);
        }
    }

    g_ptr_array_unref(paths);
    return ok;
}

static const char *language_code(const char *language) {
    if (!language) return "en";
    if (g_ascii_strcasecmp(language, "English") == 0) return "en";
    if (g_ascii_strcasecmp(language, "Spanish") == 0) return "es";
    if (g_ascii_strcasecmp(language, "Chinese") == 0) return "zh";
    if (g_ascii_strcasecmp(language, "Hindi") == 0) return "hi";
    if (g_ascii_strcasecmp(language, "Urdu") == 0) return "ur";
    if (g_ascii_strcasecmp(language, "Bangla") == 0 || g_ascii_strcasecmp(language, "Bengali") == 0) return "bn";
    if (g_ascii_strcasecmp(language, "Japanese") == 0) return "ja";
    if (g_ascii_strcasecmp(language, "Arabic") == 0) return "ar";
    if (g_ascii_strcasecmp(language, "Russian") == 0) return "ru";
    if (strlen(language) >= 2 && strlen(language) <= 3) return language;
    return "en";
}

static char *json_language_array(const char *language) {
    return g_strdup_printf("[\"%s\"]", language_code(language));
}

static char *quoted_directive_value(const char *line) {
    const char *start = strchr(line, '"');
    if (!start) return NULL;
    start++;
    const char *end = strchr(start, '"');
    if (!end) return NULL;
    return g_strndup(start, (gsize)(end - start));
}

static void parse_directive(const char *line, DslMeta *meta) {
    if (g_str_has_prefix(line, "#NAME")) {
        g_free(meta->name);
        meta->name = quoted_directive_value(line);
    } else if (g_str_has_prefix(line, "#INDEX_LANGUAGE")) {
        char *value = quoted_directive_value(line);
        g_free(meta->index_languages);
        meta->index_languages = json_language_array(value);
        g_free(value);
    } else if (g_str_has_prefix(line, "#CONTENTS_LANGUAGE")) {
        char *value = quoted_directive_value(line);
        g_free(meta->content_languages);
        meta->content_languages = json_language_array(value);
        g_free(value);
    }
}

static gboolean read_header_directives(const char *path, DslMeta *meta, GError **error) {
    DslLineReader reader;
    if (!dsl_line_reader_open(&reader, path, error)) return FALSE;

    char *line = NULL;
    while (dsl_line_reader_read_line(&reader, &line, error)) {
        char *trimmed = g_strstrip(line);
        if (trimmed[0] == '#') parse_directive(trimmed, meta);
        else if (trimmed[0] != '\0') break;
    }

    g_free(line);
    dsl_line_reader_close(&reader);
    return TRUE;
}

static gboolean is_headword_line(const char *line) {
    if (!line || !line[0]) return FALSE;
    if (line[0] == '#') return FALSE;
    return !g_ascii_isspace(line[0]);
}

static void write_entry_open(FILE *out, const char *headword) {
    fputs("<article id=\"", out);
    diction_print_html_attr(out, headword);
    fputs("\" class=\"entry\">\n", out);
    fputs("  <header>\n    <h1 class=\"headword\">", out);
    diction_print_html_text(out, headword);
    fputs("</h1>\n  </header>\n", out);
}

static void write_entry_close(FILE *out) {
    fputs("</article>\n\n", out);
}

static gboolean starts_tag(const char *s, const char *tag) {
    return g_ascii_strncasecmp(s, tag, strlen(tag)) == 0;
}

static const char *write_simple_tag(FILE *out, const char *p) {
    if (starts_tag(p, "[b]")) { fputs("<strong>", out); return p + 3; }
    if (starts_tag(p, "[/b]")) { fputs("</strong>", out); return p + 4; }
    if (starts_tag(p, "[i]")) { fputs("<em>", out); return p + 3; }
    if (starts_tag(p, "[/i]")) { fputs("</em>", out); return p + 4; }
    if (starts_tag(p, "[u]")) { fputs("<u>", out); return p + 3; }
    if (starts_tag(p, "[/u]")) { fputs("</u>", out); return p + 4; }
    if (starts_tag(p, "[sup]")) { fputs("<sup>", out); return p + 5; }
    if (starts_tag(p, "[/sup]")) { fputs("</sup>", out); return p + 6; }
    if (starts_tag(p, "[sub]")) { fputs("<sub>", out); return p + 5; }
    if (starts_tag(p, "[/sub]")) { fputs("</sub>", out); return p + 6; }
    return NULL;
}

static const char *write_reference_tag(FILE *out, const char *p) {
    const char *close = g_strstr_len(p, -1, "[/ref]");
    if (!starts_tag(p, "[ref]") || !close) return NULL;

    char *target = g_strndup(p + 5, (gsize)(close - (p + 5)));
    g_strstrip(target);
    fputs("<a href=\"entry://", out);
    diction_print_html_attr(out, target);
    fputs("\">", out);
    diction_print_html_text(out, target);
    fputs("</a>", out);
    g_free(target);
    return close + 6;
}

static const char *write_url_tag(FILE *out, const char *p) {
    const char *close = g_strstr_len(p, -1, "[/url]");
    if (!starts_tag(p, "[url]") || !close) return NULL;

    char *url = g_strndup(p + 5, (gsize)(close - (p + 5)));
    g_strstrip(url);
    fputs("<a href=\"", out);
    diction_print_html_attr(out, url);
    fputs("\">", out);
    diction_print_html_text(out, url);
    fputs("</a>", out);
    g_free(url);
    return close + 6;
}

static const char *skip_unknown_tag(const char *p) {
    if (*p != '[') return NULL;
    const char *end = strchr(p, ']');
    if (!end) return NULL;
    return end + 1;
}

static void write_dsl_inline(FILE *out, const char *text) {
    const char *p = text;
    while (*p) {
        const char *next = write_simple_tag(out, p);
        if (!next) next = write_reference_tag(out, p);
        if (!next) next = write_url_tag(out, p);
        if (!next && (starts_tag(p, "[m") || starts_tag(p, "[/m") ||
                      starts_tag(p, "[c ") || starts_tag(p, "[/c]") ||
                      starts_tag(p, "[p]") || starts_tag(p, "[/p]") ||
                      starts_tag(p, "[trn]") || starts_tag(p, "[/trn]") ||
                      starts_tag(p, "[com]") || starts_tag(p, "[/com]"))) {
            next = skip_unknown_tag(p);
        }
        if (next) {
            p = next;
            continue;
        }

        if (*p == '&') fputs("&amp;", out);
        else if (*p == '<') fputs("&lt;", out);
        else if (*p == '>') fputs("&gt;", out);
        else fputc(*p, out);
        p++;
    }
}

static void write_body_line(FILE *out, char *line) {
    char *trimmed = g_strstrip(line);
    if (!trimmed[0]) return;

    if (starts_tag(trimmed, "[ex]")) {
        char *end = g_strstr_len(trimmed, -1, "[/ex]");
        fputs("  <section class=\"examples\"><blockquote class=\"example\">", out);
        if (end) {
            *end = '\0';
            write_dsl_inline(out, trimmed + 4);
        } else {
            write_dsl_inline(out, trimmed + 4);
        }
        fputs("</blockquote></section>\n", out);
        return;
    }

    if (starts_tag(trimmed, "[trn]")) {
        char *end = g_strstr_len(trimmed, -1, "[/trn]");
        fputs("  <section class=\"translations\"><div class=\"translation\">", out);
        if (end) {
            *end = '\0';
            write_dsl_inline(out, trimmed + 5);
        } else {
            write_dsl_inline(out, trimmed + 5);
        }
        fputs("</div></section>\n", out);
        return;
    }

    fputs("  <section class=\"definitions\"><p>", out);
    write_dsl_inline(out, trimmed);
    fputs("</p></section>\n", out);
}

static gboolean convert_dsl(const char *path, const DictionOutput *out, GError **error) {
    DslLineReader reader;
    if (!dsl_line_reader_open(&reader, path, error)) return FALSE;

    FILE *html = fopen(out->html_path, "w");
    if (!html) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create %s", out->html_path);
        dsl_line_reader_close(&reader);
        return FALSE;
    }

    char *line = NULL;
    gboolean in_entry = FALSE;

    fprintf(stderr, "Converting DSL entries into Diction HTML: %s\n", out->html_path);
    while (dsl_line_reader_read_line(&reader, &line, error)) {
        if (line[0] == '#') continue;

        if (is_headword_line(line)) {
            if (in_entry) write_entry_close(html);
            char *headword = g_strstrip(line);
            write_entry_open(html, headword);
            in_entry = TRUE;
        } else if (in_entry) {
            write_body_line(html, line);
        }
    }

    if (in_entry) write_entry_close(html);
    g_free(line);
    fclose(html);
    dsl_line_reader_close(&reader);
    return TRUE;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.dsl [output_root]\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    if (!g_file_test(input_path, G_FILE_TEST_EXISTS)) {
        fprintf(stderr, "Error: input path does not exist: %s\n", input_path);
        return 1;
    }

    GError *error = NULL;
    DslMeta meta = {0};
    char *stem = stem_from_path(input_path);
    DictionOutput *out = diction_output_new(stem, argc > 2 ? argv[2] : NULL);

    gboolean ok = g_mkdir_with_parents(out->dict_dir, 0755) == 0;
    if (!ok) {
        g_set_error(&error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create %s", out->dict_dir);
    }

    if (ok) ok = read_header_directives(input_path, &meta, &error);
    if (ok && !meta.name) meta.name = g_strdup(stem);
    if (ok && !meta.index_languages) meta.index_languages = g_strdup("[\"en\"]");
    if (ok && !meta.content_languages) meta.content_languages = g_strdup("[\"en\"]");
    if (ok) ok = convert_dsl(input_path, out, &error);
    if (ok) ok = diction_write_meta(out->meta_path, out->dict_name, meta.name, out->dict_name,
                                    out->html_name, meta.index_languages, meta.content_languages, &error);
    if (ok) ok = diction_write_standard_css(out->css_path, &error);
    if (ok) ok = extract_related_media(input_path, out, &error);
    if (ok) ok = diction_package(out, &error);

    if (ok) {
        fprintf(stderr, "Done. Output directory: %s\n", out->output_root);
    } else {
        fprintf(stderr, "Error: %s\n", error ? error->message : "conversion failed");
    }

    g_clear_error(&error);
    g_free(meta.name);
    g_free(meta.index_languages);
    g_free(meta.content_languages);
    diction_output_free(out);
    g_free(stem);
    return ok ? 0 : 1;
}
