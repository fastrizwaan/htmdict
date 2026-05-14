#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <libgen.h>
#include "mdict-parser.h"

/* Helper to normalize media paths like src="/foo.png" to src="media/foo.png" */
void print_normalized_content(FILE *out, const uint8_t *data, size_t len) {
    const uint8_t *p = data;
    const uint8_t *end = data + len;
    while (p < end) {
        if (end - p >= 6 && strncmp((const char*)p, "src=\"/", 6) == 0) {
            fprintf(out, "src=\"media/");
            p += 6;
        } else if (end - p >= 6 && strncmp((const char*)p, "src='/", 6) == 0) {
            fprintf(out, "src='media/");
            p += 6;
        } else if (end - p >= 7 && strncmp((const char*)p, "href=\"/", 7) == 0) {
            fprintf(out, "href=\"media/");
            p += 7;
        } else if (end - p >= 7 && strncmp((const char*)p, "href='/", 7) == 0) {
            fprintf(out, "href='media/");
            p += 7;
        } else {
            fputc(*p, out);
            p++;
        }
    }
    fputc('\n', out);
}

/* Helper to escape double quotes in HTML attributes */
void print_escaped_id(FILE *out, const char *s) {
    while (*s) {
        if (*s == '"') fprintf(out, "&quot;");
        else fputc(*s, out);
        s++;
    }
}

typedef struct {
    FILE *out;
} ExtractContext;

static void mdx_entry_callback(const char *key, const uint8_t *data, size_t len, gpointer user_data) {
    ExtractContext *ctx = user_data;
    fprintf(ctx->out, "<article id=\"");
    print_escaped_id(ctx->out, key);
    fprintf(ctx->out, "\" class=\"entry\">\n");
    
    /* Check if it's a link */
    if (len >= 8 && strncmp((const char*)data, "@@@LINK=", 8) == 0) {
        /* Extract target word */
        const char *target_start = (const char*)data + 8;
        const char *target_end = target_start;
        while (target_end < (const char*)data + len && *target_end != '\r' && *target_end != '\n' && *target_end != '\0') {
            target_end++;
        }
        char *target = g_strndup(target_start, (gsize)(target_end - target_start));
        char *trimmed = g_strstrip(target);
        fprintf(ctx->out, "<a href=\"entry://%s\">@@@LINK=%s</a>\n", trimmed, trimmed);
        g_free(target);
    } else {
        print_normalized_content(ctx->out, data, len);
    }
    fprintf(ctx->out, "</article>\n");
}

static void mdd_resource_callback(const char *key, const uint8_t *data, size_t len, gpointer user_data) {
    if (!key || !key[0]) return;
    fprintf(stderr, "Extracting %s (%zu bytes)\n", key, len);
    const char *output_dir = user_data;
    char *path = g_strdup(key);
    /* Replace backslashes with slashes */
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    
    /* Remove leading slash if any */
    const char *rel_path = (path[0] == '/') ? path + 1 : path;
    
    char *full_path = g_build_filename(output_dir, "media", rel_path, NULL);
    char *dir = g_path_get_dirname(full_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    
    FILE *f = fopen(full_path, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
    g_free(full_path);
    g_free(path);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.mdx|input.mdd|input.txt.html [output_dir]\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    char *input_dir_name = g_path_get_dirname(input_path);
    char *base_name = g_path_get_basename(input_path);
    char *stem = g_strdup(base_name);
    char *ext = strrchr(stem, '.');
    
    /* Handle .txt.html specifically */
    if (ext && g_ascii_strcasecmp(ext, ".html") == 0) {
        char *p = stem + (ext - stem);
        if (p >= stem + 4 && g_ascii_strncasecmp(p - 4, ".txt", 4) == 0) {
            *(p - 4) = '\0';
        } else {
            *ext = '\0';
        }
    } else if (ext) {
        *ext = '\0';
    }

    char *output_dir = (argc > 2) ? g_strdup(argv[2]) : g_strdup_printf("%s_diction", stem);
    g_mkdir_with_parents(output_dir, 0755);

    gboolean processed = FALSE;
    GError *err = NULL;

    /* Check for MDX and MDD pairs */
    char *mdx_path = g_strdup_printf("%s/%s.mdx", input_dir_name, stem);
    char *mdd_path = g_strdup_printf("%s/%s.mdd", input_dir_name, stem);
    char *legacy_path = g_strdup_printf("%s/%s.txt.html", input_dir_name, stem);

    if (g_file_test(mdx_path, G_FILE_TEST_EXISTS)) {
        MdictReader *r = mdict_reader_open(mdx_path, &err);
        if (r) {
            char html_path[1024];
            snprintf(html_path, sizeof(html_path), "%s/%s.html", output_dir, stem);
            FILE *out = fopen(html_path, "w");
            if (out) {
                ExtractContext ctx = { .out = out };
                fprintf(stderr, "Extracting entries from %s...\n", mdx_path);
                mdict_reader_iterate(r, mdx_entry_callback, &ctx, &err);
                fclose(out);
                
                /* Generate meta.json */
                char meta_path[1024];
                snprintf(meta_path, sizeof(meta_path), "%s/meta.json", output_dir);
                FILE *meta = fopen(meta_path, "w");
                if (meta) {
                    fprintf(meta, "{\n  \"format\": 1,\n  \"id\": \"%s\",\n  \"name\": \"%s\",\n", stem, mdict_reader_get_title(r) ? mdict_reader_get_title(r) : stem);
                    fprintf(meta, "  \"index_languages\": [\"en\"],\n  \"content_languages\": [\"en\"],\n");
                    fprintf(meta, "  \"version\": \"1.0\",\n  \"stylesheet\": \"style.css\",\n  \"html\": \"%s.html\"\n}\n", stem);
                    fclose(meta);
                }
                /* style.css */
                char css_path[1024];
                snprintf(css_path, sizeof(css_path), "%s/style.css", output_dir);
                FILE *css = fopen(css_path, "a"); if (css) fclose(css);
                processed = TRUE;
            }
            mdict_reader_close(r);
        }
    }

    if (g_file_test(mdd_path, G_FILE_TEST_EXISTS)) {
        MdictReader *r = mdict_reader_open(mdd_path, &err);
        if (r) {
            fprintf(stderr, "Extracting resources from %s...\n", mdd_path);
            mdict_reader_iterate(r, mdd_resource_callback, output_dir, &err);
            mdict_reader_close(r);
            processed = TRUE;
        }
    }

    if (!processed && g_file_test(legacy_path, G_FILE_TEST_EXISTS)) {
        FILE *in = fopen(legacy_path, "r");
        if (in) {
            char html_path[1024];
            snprintf(html_path, sizeof(html_path), "%s/%s.html", output_dir, stem);
            FILE *out = fopen(html_path, "w");
            if (out) {
                fprintf(stderr, "Converting legacy source %s -> %s\n", legacy_path, html_path);
                char *line = NULL; size_t len = 0; ssize_t read; char *headword = NULL; int state = 0;
                while ((read = getline(&line, &len, in)) != -1) {
                    if (read > 0 && line[read - 1] == '\n') line[--read] = '\0';
                    if (read > 0 && line[read - 1] == '\r') line[--read] = '\0';
                    if (state == 0) {
                        if (read == 0 || strcmp(line, "</>") == 0) continue;
                        headword = g_strdup(line);
                        fprintf(out, "<article id=\""); print_escaped_id(out, headword); fprintf(out, "\" class=\"entry\">\n");
                        state = 1;
                    } else {
                        if (strcmp(line, "</>") == 0) {
                            fprintf(out, "</article>\n"); g_free(headword); headword = NULL; state = 0;
                        } else {
                            if (strncmp(line, "@@@LINK=", 8) == 0) fprintf(out, "<a href=\"entry://%s\">%s</a>\n", line + 8, line);
                            else print_normalized_content(out, (const uint8_t*)line, (size_t)read);
                        }
                    }
                }
                if (state == 1) { fprintf(out, "</article>\n"); g_free(headword); }
                fclose(out);
                
                char meta_path[1024]; snprintf(meta_path, sizeof(meta_path), "%s/meta.json", output_dir);
                FILE *meta = fopen(meta_path, "w");
                if (meta) {
                    fprintf(meta, "{\n  \"format\": 1,\n  \"id\": \"%s\",\n  \"name\": \"%s\",\n", stem, stem);
                    fprintf(meta, "  \"index_languages\": [\"en\"],\n  \"content_languages\": [\"en\"],\n");
                    fprintf(meta, "  \"version\": \"1.0\",\n  \"stylesheet\": \"style.css\",\n  \"html\": \"%s.html\"\n}\n", stem);
                    fclose(meta);
                }
                char css_path[1024]; snprintf(css_path, sizeof(css_path), "%s/style.css", output_dir);
                FILE *css = fopen(css_path, "a"); if (css) fclose(css);
                processed = TRUE;
                g_free(line);
            }
            fclose(in);
        }
    }

    if (processed) {
        fprintf(stderr, "Packaging into %s.diction...\n", stem);
        char *cmd = g_strdup_printf("cd '%s' && zip -rq '../%s.diction' .", output_dir, stem);
        if (system(cmd) != 0) {
            fprintf(stderr, "Warning: Failed to create .diction file. Is 'zip' installed?\n");
        }
        g_free(cmd);
        fprintf(stderr, "Done. Output in %s/ and %s.diction\n", output_dir, stem);
    } else {
        fprintf(stderr, "Error: Could not find any valid MDict or legacy files for '%s'\n", stem);
    }

    g_free(mdx_path); g_free(mdd_path); g_free(legacy_path);
    g_free(stem); g_free(base_name); g_free(input_dir_name); g_free(output_dir);
    if (err) { fprintf(stderr, "Error: %s\n", err->message); g_error_free(err); return 1; }
    
    return processed ? 0 : 1;
}
