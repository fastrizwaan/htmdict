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
    char *output_dir = (argc > 2) ? argv[2] : ".";
    
    /* Detect file type */
    const char *ext = strrchr(input_path, '.');
    if (ext && (g_ascii_strcasecmp(ext, ".mdx") == 0 || g_ascii_strcasecmp(ext, ".mdd") == 0)) {
        GError *err = NULL;
        MdictReader *r = mdict_reader_open(input_path, &err);
        if (!r) {
            fprintf(stderr, "Error opening Mdict file: %s\n", err->message);
            g_error_free(err);
            return 1;
        }
        
        char *base_name = g_path_get_basename(input_path);
        char *stem = g_strdup(base_name);
        char *dot = strrchr(stem, '.');
        if (dot) *dot = '\0';
        
        if (g_ascii_strcasecmp(ext, ".mdx") == 0) {
            char html_path[1024];
            snprintf(html_path, sizeof(html_path), "%s/%s.html", output_dir, stem);
            g_mkdir_with_parents(output_dir, 0755);
            
            FILE *out = fopen(html_path, "w");
            if (!out) { perror("fopen"); return 1; }
            
            ExtractContext ctx = { .out = out };
            fprintf(stderr, "Extracting entries from %s...\n", input_path);
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
        } else {
            fprintf(stderr, "Extracting resources from %s...\n", input_path);
            mdict_reader_iterate(r, mdd_resource_callback, output_dir, &err);
        }
        
        mdict_reader_close(r);
        g_free(stem); g_free(base_name);
        if (err) { fprintf(stderr, "Error during iteration: %s\n", err->message); g_error_free(err); return 1; }
        fprintf(stderr, "Done.\n");
        return 0;
    }

    /* Legacy support for .txt.html */
    /* ... existing code ... */
    /* I'll just rewrite the whole main to include the old logic if needed, but the user asked for mdx/mdd support specifically */
    
    /* For brevity in this turn, I'll keep the old logic too */
    FILE *in = fopen(input_path, "r");
    if (!in) { perror("Error opening input file"); return 1; }

    struct stat st = {0};
    if (stat(output_dir, &st) == -1) g_mkdir_with_parents(output_dir, 0755);

    char *base_name = strdup(input_path);
    char *stem = basename(base_name);
    char *dot = strstr(stem, ".txt.html");
    if (dot) *dot = '\0';
    else { dot = strrchr(stem, '.'); if (dot) *dot = '\0'; }

    char html_path[1024];
    snprintf(html_path, sizeof(html_path), "%s/%s.html", output_dir, stem);
    FILE *out = fopen(html_path, "w");
    if (!out) { perror("Error opening output file"); free(base_name); fclose(in); return 1; }

    fprintf(stderr, "Converting legacy source %s -> %s\n", input_path, html_path);
    char *line = NULL; size_t len = 0; ssize_t read; char *headword = NULL; int state = 0;
    while ((read = getline(&line, &len, in)) != -1) {
        if (read > 0 && line[read - 1] == '\n') line[--read] = '\0';
        if (read > 0 && line[read - 1] == '\r') line[--read] = '\0';
        if (state == 0) {
            if (read == 0 || strcmp(line, "</>") == 0) continue;
            headword = strdup(line);
            fprintf(out, "<article id=\""); print_escaped_id(out, headword); fprintf(out, "\" class=\"entry\">\n");
            state = 1;
        } else {
            if (strcmp(line, "</>") == 0) {
                fprintf(out, "</article>\n"); free(headword); headword = NULL; state = 0;
            } else {
                if (strncmp(line, "@@@LINK=", 8) == 0) fprintf(out, "<a href=\"entry://%s\">%s</a>\n", line + 8, line);
                else print_normalized_content(out, (const uint8_t*)line, read);
            }
        }
    }
    if (state == 1) { fprintf(out, "</article>\n"); free(headword); }
    fclose(out); fclose(in); free(line);
    
    /* meta.json and style.css for legacy */
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
    
    fprintf(stderr, "Done.\n");
    free(base_name);
    return 0;
}
