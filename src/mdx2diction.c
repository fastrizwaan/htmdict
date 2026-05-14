#include "diction-convert.h"
#include "mdict-parser.h"
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *out;
} EntryContext;

static char *stem_from_input(const char *input_path) {
    if (g_file_test(input_path, G_FILE_TEST_IS_DIR)) {
        return g_path_get_basename(input_path);
    }

    char *base = g_path_get_basename(input_path);
    char *stem = g_strdup(base);
    char *ext = strrchr(stem, '.');
    if (ext && g_ascii_strcasecmp(ext, ".html") == 0) {
        char *txt = ext - 4;
        if (txt >= stem && g_ascii_strncasecmp(txt, ".txt", 4) == 0) *txt = '\0';
        else *ext = '\0';
    } else if (ext) {
        *ext = '\0';
    }
    g_free(base);
    return stem;
}

static void write_entry_open(FILE *out, const char *key) {
    fputs("<article id=\"", out);
    diction_print_html_attr(out, key);
    fputs("\" class=\"entry\">\n", out);
    fputs("  <header>\n    <h1 class=\"headword\">", out);
    diction_print_html_text(out, key);
    fputs("</h1>\n  </header>\n", out);
}

static char *extract_link_target(const uint8_t *data, size_t len) {
    const char *start = (const char *)data + 8;
    const char *end = (const char *)data + len;
    const char *p = start;
    while (p < end && *p != '\r' && *p != '\n' && *p != '\0') p++;
    char *target = g_strndup(start, (gsize)(p - start));
    g_strstrip(target);
    return target;
}

static void mdx_entry_callback(const char *key, const uint8_t *data, size_t len, gpointer user_data) {
    EntryContext *ctx = user_data;

    write_entry_open(ctx->out, key);
    if (len >= 8 && strncmp((const char *)data, "@@@LINK=", 8) == 0) {
        char *target = extract_link_target(data, len);
        fputs("  <section class=\"cross-reference\">\n    <a href=\"entry://", ctx->out);
        diction_print_html_attr(ctx->out, target);
        fputs("\">", ctx->out);
        diction_print_html_text(ctx->out, target);
        fputs("</a>\n  </section>\n", ctx->out);
        g_free(target);
    } else {
        fputs("  <section class=\"definitions\">\n", ctx->out);
        diction_print_normalized_fragment(ctx->out, data, len);
        fputs("\n  </section>\n", ctx->out);
    }
    fputs("</article>\n\n", ctx->out);
}

static void mdd_resource_callback(const char *key, const uint8_t *data, size_t len, gpointer user_data) {
    if (!key || !key[0]) return;

    const char *dict_dir = user_data;
    char *path = g_strdup(key);
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    const char *rel_path = path[0] == '/' ? path + 1 : path;
    char *full_path = g_build_filename(dict_dir, "media", rel_path, NULL);
    char *dir = g_path_get_dirname(full_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    FILE *f = fopen(full_path, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
    } else {
        fprintf(stderr, "Warning: could not write resource %s\n", full_path);
    }

    g_free(full_path);
    g_free(path);
}

static gboolean convert_mdx(const char *mdx_path, const DictionOutput *out, const char *fallback_name, GError **error) {
    MdictReader *reader = mdict_reader_open(mdx_path, error);
    if (!reader) return FALSE;

    FILE *html = fopen(out->html_path, "w");
    if (!html) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create %s", out->html_path);
        mdict_reader_close(reader);
        return FALSE;
    }

    EntryContext ctx = { .out = html };
    fprintf(stderr, "Extracting MDX entries into Diction HTML: %s\n", out->html_path);
    gboolean ok = mdict_reader_iterate(reader, mdx_entry_callback, &ctx, error);
    fclose(html);

    const char *title = mdict_reader_get_title(reader);
    if (!title || !title[0]) title = fallback_name;

    if (ok) {
        ok = diction_write_meta(out->meta_path, out->dict_name, title, out->dict_name,
                                out->html_name, "[\"en\"]", "[\"en\"]", error);
    }

    mdict_reader_close(reader);
    return ok;
}

static gboolean convert_legacy_txt_html(const char *legacy_path, const DictionOutput *out, GError **error) {
    FILE *in = fopen(legacy_path, "r");
    if (!in) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not open %s", legacy_path);
        return FALSE;
    }

    FILE *html = fopen(out->html_path, "w");
    if (!html) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create %s", out->html_path);
        fclose(in);
        return FALSE;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    char *headword = NULL;
    gboolean in_entry = FALSE;

    fprintf(stderr, "Converting legacy source into Diction HTML: %s\n", out->html_path);
    while ((nread = getline(&line, &cap, in)) != -1) {
        while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
            line[--nread] = '\0';
        }

        if (!in_entry) {
            if (nread == 0 || strcmp(line, "</>") == 0) continue;
            headword = g_strdup(line);
            write_entry_open(html, headword);
            fputs("  <section class=\"definitions\">\n", html);
            in_entry = TRUE;
            continue;
        }

        if (strcmp(line, "</>") == 0) {
            fputs("  </section>\n</article>\n\n", html);
            g_clear_pointer(&headword, g_free);
            in_entry = FALSE;
        } else if (strncmp(line, "@@@LINK=", 8) == 0) {
            char *target = g_strdup(line + 8);
            g_strstrip(target);
            fputs("    <a href=\"entry://", html);
            diction_print_html_attr(html, target);
            fputs("\">", html);
            diction_print_html_text(html, target);
            fputs("</a>\n", html);
            g_free(target);
        } else {
            fputs("    ", html);
            diction_print_normalized_fragment(html, (const uint8_t *)line, (size_t)nread);
            fputc('\n', html);
        }
    }

    if (in_entry) {
        fputs("  </section>\n</article>\n", html);
        g_free(headword);
    }

    g_free(line);
    fclose(html);
    fclose(in);

    return diction_write_meta(out->meta_path, out->dict_name, out->dict_name, out->dict_name,
                              out->html_name, "[\"en\"]", "[\"en\"]", error);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.mdx|input.mdd|input.txt.html|directory [output_root]\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    if (!g_file_test(input_path, G_FILE_TEST_EXISTS)) {
        fprintf(stderr, "Error: input path does not exist: %s\n", input_path);
        return 1;
    }

    char *stem = stem_from_input(input_path);
    char *input_dir = g_file_test(input_path, G_FILE_TEST_IS_DIR)
        ? g_strdup(input_path)
        : g_path_get_dirname(input_path);
    DictionOutput *out = diction_output_new(stem, argc > 2 ? argv[2] : NULL);

    GError *error = NULL;
    gboolean processed = FALSE;
    if (g_mkdir_with_parents(out->dict_dir, 0755) != 0) {
        fprintf(stderr, "Error: could not create %s\n", out->dict_dir);
        diction_output_free(out);
        g_free(input_dir);
        g_free(stem);
        return 1;
    }

    char *mdx_path = g_strdup_printf("%s/%s.mdx", input_dir, stem);
    char *mdd_path = g_strdup_printf("%s/%s.mdd", input_dir, stem);
    char *legacy_path = g_strdup_printf("%s/%s.txt.html", input_dir, stem);

    if (g_ascii_strcasecmp(input_path + MAX(0, (int)strlen(input_path) - 4), ".mdx") == 0) {
        g_free(mdx_path);
        mdx_path = g_strdup(input_path);
    } else if (g_ascii_strcasecmp(input_path + MAX(0, (int)strlen(input_path) - 4), ".mdd") == 0) {
        g_free(mdd_path);
        mdd_path = g_strdup(input_path);
    }

    if (g_file_test(mdx_path, G_FILE_TEST_EXISTS)) {
        processed = convert_mdx(mdx_path, out, stem, &error);
    } else if (g_file_test(legacy_path, G_FILE_TEST_EXISTS)) {
        processed = convert_legacy_txt_html(legacy_path, out, &error);
    }

    if (processed && g_file_test(mdd_path, G_FILE_TEST_EXISTS)) {
        MdictReader *resources = mdict_reader_open(mdd_path, &error);
        if (resources) {
            fprintf(stderr, "Extracting MDD resources into %s/media\n", out->dict_dir);
            if (!mdict_reader_iterate(resources, mdd_resource_callback, out->dict_dir, &error)) {
                processed = FALSE;
            }
            mdict_reader_close(resources);
        } else {
            fprintf(stderr, "Warning: could not open MDD resources: %s\n", error ? error->message : "unknown error");
            g_clear_error(&error);
        }
    }

    if (processed) processed = diction_write_standard_css(out->css_path, &error);
    if (processed) processed = diction_package(out, &error);

    if (processed) {
        fprintf(stderr, "Done. Output directory: %s\n", out->output_root);
    } else {
        fprintf(stderr, "Error: %s\n", error ? error->message : "no MDX or legacy source found");
    }

    g_clear_error(&error);
    g_free(mdx_path);
    g_free(mdd_path);
    g_free(legacy_path);
    diction_output_free(out);
    g_free(input_dir);
    g_free(stem);
    return processed ? 0 : 1;
}
